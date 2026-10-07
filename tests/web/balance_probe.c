/* Finite balancing sample. Decisions use only current public actors, bullets,
 * HUD availability and accepted-count feedback. Metrics read real events.
 * No HP/projectile injection, RNG lookahead, copied-World rollout or ML. */
#include "world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RUN_TICKS 3600
#define CPU_LIMIT 60.0
typedef struct VisibleBullet { float x,y,vx,vy,r; } VisibleBullet;
typedef struct PublicState {
    Actor boss, students[DEMO_MAX_STUDENTS];
    uint32_t students_count, bullet_count, accepted;
    VisibleBullet bullets[DEMO_MAX_PROJECTILES];
    bool available[4];
    int32_t tick;
} PublicState;
typedef struct SkillMetrics { unsigned accepted, spent, damage, kills, rejected; } SkillMetrics;
typedef struct Result {
    uint32_t seed, initial_students;
    unsigned movement, attack, ticks, first_clear, waves, wave, kills, deployed, overflow;
    int hp, status;
    SkillMetrics skills[4];
} Result;
static const char *move_names[] = {"stationary", "chase-48-avoid20", "chase-150-avoid4", "orbit-150-avoid4"};
static const char *attack_names[] = {"ring", "course", "mine", "shower", "mixed", "none"};
static const float dirs[9][2] = {{0,0},{1,0},{.70710677f,.70710677f},{0,1},{-.70710677f,.70710677f},{-1,0},{-.70710677f,-.70710677f},{0,-1},{.70710677f,-.70710677f}};
static void observe(const WorldView *v, PublicState *s) {
    memset(s,0,sizeof(*s)); s->boss=*v->boss; s->students_count=v->student_count;
    memcpy(s->students,v->students,v->student_count*sizeof(Actor));
    memcpy(s->available,v->pattern_available,sizeof(s->available));
    s->tick=v->tick;s->accepted=v->attack_accept_count;
    for(unsigned i=0;i<v->projectile_capacity;++i) {
        const Projectile *p=&v->projectiles[i];
        if(!p->active || p->faction!=DEMO_FACTION_STUDENT || p->x<0||p->x>960||p->y<0||p->y>720)continue;
        VisibleBullet *b=&s->bullets[s->bullet_count++];
        b->x=p->x;b->y=p->y;b->vx=p->vx;b->vy=p->vy;b->r=p->radius;
    }
}
static float risk(const PublicState *s,float mx,float my) {
    float score=0;
    for(unsigned i=0;i<s->bullet_count;++i) {
        const VisibleBullet *b=&s->bullets[i];float rx=b->x-s->boss.x,ry=b->y-s->boss.y;
        float vx=b->vx-mx*270,vy=b->vy-my*270,vv=vx*vx+vy*vy;
        float t=vv>0?-(rx*vx+ry*vy)/vv:0;if(t<0)t=0;if(t>.45f)t=.45f;
        float d=hypotf(rx+vx*t,ry+vy*t),safe=22+b->r+10;
        if(d<safe)score+=1+(safe-d)/safe;
    }return score;
}
static void choose(const PublicState *s,unsigned movement,unsigned attack,int *last_request,unsigned *next,unsigned *last_accepted,BossInput *in) {
    boss_input_clear(in);
    if(s->accepted>*last_accepted) { *next=(*next+1)%4;*last_accepted=s->accepted; }
    int target=-1;float d2=0;
    for(unsigned i=0;i<s->students_count;++i)if(s->students[i].alive) {
        float dx=s->students[i].x-s->boss.x,dy=s->students[i].y-s->boss.y,d=dx*dx+dy*dy;
        if(target<0||d<d2){target=(int)i;d2=d;}
    }
    if(target<0)return;
    float d=sqrtf(d2),ux=d>0?(s->students[target].x-s->boss.x)/d:0,uy=d>0?(s->students[target].y-s->boss.y)/d:0;
    if(movement) {
        float wanted=movement==1?48:150,dx=0,dy=0;
        if(movement<3) { if(d>wanted+8){dx=ux;dy=uy;} else if(d<wanted-8){dx=-ux;dy=-uy;} }
        else {float radial=(d-wanted)/80;if(radial<-1)radial=-1;if(radial>1)radial=1;dx=ux*radial-uy;dy=uy*radial+ux;}
        float length=hypotf(dx,dy);if(length>0){dx/=length;dy/=length;}
        float best=1e30f;unsigned index=0;
        for(unsigned j=0;j<9;++j){float mx=dirs[j][0],my=dirs[j][1],ex=mx-dx,ey=my-dy;
            float score=ex*ex+ey*ey+(movement==1?20:4)*risk(s,mx,my);
            float x=s->boss.x+mx*270*.35f,y=s->boss.y+my*270*.35f;
            if(x<22||x>938||y<22||y>698)score+=8;
            if(score<best){best=score;index=j;}
        }in->move_x=dirs[index][0];in->move_y=dirs[index][1];
    }
    if(attack<5 && s->tick-*last_request>=12) {
        unsigned p=attack==4?*next:attack;
        if(s->available[p]){in->attack_requested[p]=true;*last_request=s->tick;}
    }
}
static Result run(const DemoConfig *base,uint32_t seed,unsigned students,unsigned movement,unsigned attack) {
    DemoConfig cfg=*base;cfg.student_count=students;
    World w;WorldView v;PublicState state;BossInput in;int last=-12;unsigned next=0,last_accepted=0;
    Result r;memset(&r,0,sizeof(r));r.seed=seed;r.initial_students=students;r.movement=movement;r.attack=attack;
    if(!world_reset(&w,&cfg,seed)){fprintf(stderr,"reset failed\n");exit(2);}
    world_make_view(&w,&v);
    while(v.status==DEMO_STATUS_RUNNING&&!v.truncated&&v.tick<RUN_TICKS) {
        observe(&v,&state);choose(&state,movement,attack,&last,&next,&last_accepted,&in);world_step(&w,&in);world_make_view(&w,&v);
        for(unsigned i=0;i<v.event_count;++i) {
            const StepEvent *e=&v.events[i];unsigned p=(unsigned)e->pattern;if(p>=4)continue;
            if(e->type==DEMO_EVENT_ATTACK_ACCEPTED)r.skills[p].accepted++;
            else if(e->type==DEMO_EVENT_ATTACK_REJECTED)r.skills[p].rejected++;
            /* attack.c publishes the charged cost on the accepted windup. */
            else if(e->type==DEMO_EVENT_WINDUP_START)r.skills[p].spent+=(unsigned)e->amount;
            else if(e->type==DEMO_EVENT_HIT&&e->source_id==v.boss->id)r.skills[p].damage+=(unsigned)e->amount;
            else if(e->type==DEMO_EVENT_KNOCKDOWN&&e->source_id==v.boss->id)r.skills[p].kills++;
        }
        if(!r.first_clear&&v.waves_cleared)r.first_clear=(unsigned)v.tick;
    }
    r.ticks=(unsigned)v.tick;r.hp=v.boss->hp;r.status=(int)v.status;r.waves=v.waves_cleared;r.wave=v.wave_index;
    r.kills=v.students_defeated;r.deployed=v.students_deployed;r.overflow=w.spawn_overflow_count;
    return r;
}
static void write_result(FILE *f,const Result *r,bool comma) {
    fprintf(f,"%s{\"seed\":%u,\"initial_students\":%u,\"movement\":\"%s\",\"attack\":\"%s\",\"ticks\":%u,\"seconds\":%.3f,\"status\":%d,\"unfinished\":%s,\"hp\":%d,\"first_clear_tick\":%u,\"wave\":%u,\"waves_cleared\":%u,\"kills\":%u,\"deployed\":%u,\"overflow\":%u,\"skills\":[",
        comma?",\n":"",r->seed,r->initial_students,move_names[r->movement],attack_names[r->attack],r->ticks,r->ticks/60.0,r->status,r->status==0?"true":"false",r->hp,r->first_clear,r->wave,r->waves,r->kills,r->deployed,r->overflow);
    for(unsigned p=0;p<4;++p){const SkillMetrics *s=&r->skills[p];fprintf(f,"%s{\"pattern\":%u,\"accepted\":%u,\"spent\":%u,\"damage\":%u,\"kills\":%u,\"rejected\":%u,\"damage_per_100_energy\":%.6f}",p?",":"",p,s->accepted,s->spent,s->damage,s->kills,s->rejected,s->spent?100.0*s->damage/s->spent:0);}
    fprintf(f,"]}");
}
int main(int argc,char **argv) {
    const char *output=argc>1?argv[1]:"build/web-validation/balance-current.json";
    unsigned heldout=argc>2?(unsigned)atoi(argv[2]):0;
    static const uint32_t primary[]={20261006u,12345u,67890u,111111u,2024u,987654321u};
    static const uint32_t hold[]={20261007u,7654321u,424242u,314159u,888888u,13579u};
    const uint32_t *seeds=heldout?hold:primary;
    DemoConfig cfg;if(!demo_config_init(&cfg))return 2;
    /* Optional numeric-only candidate; never changes rules or injects state. */
    unsigned candidate=argc>3?(unsigned)atoi(argv[3]):0;
    /* 0 = shipped v4. Other experiments start from the captured v3 gameplay
     * numbers, while retaining the current scoring formula. 99 = before.
     * This keeps the checked-in probe reproducible after updating defaults. */
    if(candidate!=0) {
        cfg.student_fire_min_range=60;
        cfg.patterns[0].bullet_speed=180;
        cfg.patterns[1].cost=35;cfg.patterns[1].bullet_speed=220;
        cfg.patterns[1].lane_spread_px=8;
        cfg.patterns[2].windup_ticks=72;cfg.patterns[2].bullet_speed=200;
        cfg.patterns[2].spawn_safety_radius=180;
        cfg.patterns[3].wave_count=5;
    }
    if(candidate==1) {
        cfg.student_fire_min_range=50;
        cfg.patterns[1].active_ticks=240;
        cfg.patterns[2].bullet_speed=300;cfg.patterns[2].windup_ticks=48;
    } else if(candidate==2) {
        cfg.student_fire_min_range=50;
        cfg.patterns[1].active_ticks=240;
        cfg.patterns[2].bullet_speed=300;cfg.patterns[2].windup_ticks=48;
        cfg.patterns[3].wave_count=3;
    } else if(candidate==3) {
        cfg.student_fire_min_range=50;
        cfg.patterns[1].active_ticks=240;
        cfg.patterns[2].bullet_speed=300;cfg.patterns[2].windup_ticks=48;
        cfg.patterns[3].shots_per_wave=10;
    } else if(candidate==4) {
        cfg.student_fire_min_range=50;
        cfg.patterns[1].active_ticks=240;
        cfg.patterns[2].bullet_speed=300;cfg.patterns[2].windup_ticks=48;
        cfg.patterns[3].wave_count=3;cfg.patterns[3].shots_per_wave=10;
    } else if(candidate==5 || candidate==6) {
        cfg.student_fire_min_range=50;
        cfg.patterns[1].bullet_speed=candidate==5?320:440;cfg.patterns[1].cost=20;
        cfg.patterns[2].bullet_speed=300;cfg.patterns[2].windup_ticks=48;
        cfg.patterns[2].spawn_safety_radius=120;
        cfg.patterns[3].wave_count=3;
    } else if(candidate==7) {
        cfg.student_fire_min_range=50;
        cfg.patterns[1].bullet_speed=320;cfg.patterns[1].cost=20;
        cfg.patterns[2].bullet_speed=300;cfg.patterns[2].windup_ticks=48;
        cfg.patterns[2].spawn_safety_radius=120;
        cfg.patterns[3].shots_per_wave=10;
    } else if(candidate==8 || candidate==9) {
        cfg.student_fire_min_range=50;
        cfg.patterns[0].bullet_speed=230;
        cfg.patterns[1].bullet_speed=320;cfg.patterns[1].cost=20;
        if(candidate==9)cfg.patterns[1].wave_count=5;
        cfg.patterns[2].bullet_speed=300;cfg.patterns[2].windup_ticks=48;
        cfg.patterns[2].spawn_safety_radius=120;
        cfg.patterns[3].wave_count=3;
    } else if(candidate>=10 && candidate<=15) {
        cfg.student_fire_min_range=50;
        cfg.patterns[0].bullet_speed=230;
        cfg.patterns[1].bullet_speed=candidate==12?440:320;cfg.patterns[1].cost=20;
        cfg.patterns[1].windup_ticks=candidate==10?48:30;
        cfg.patterns[1].wave_count=candidate>=13?8:5;
        cfg.patterns[1].active_ticks=candidate>=13?300:240;
        if(candidate==14)cfg.patterns[1].wave_interval_sec=.3f;
        if(candidate==15)cfg.patterns[1].wave_interval_sec=.15f;
        cfg.patterns[2].bullet_speed=300;cfg.patterns[2].windup_ticks=48;
        cfg.patterns[2].spawn_safety_radius=120;
        cfg.patterns[3].wave_count=3;
    } else if(candidate>=16 && candidate<=18) {
        cfg.student_fire_min_range=50;
        cfg.patterns[0].bullet_speed=230;
        cfg.patterns[1].bullet_speed=320;
        cfg.patterns[1].cost=candidate==16?35:candidate==17?40:45;
        cfg.patterns[1].lane_spread_px=96;
        cfg.patterns[2].bullet_speed=300;cfg.patterns[2].windup_ticks=48;
        cfg.patterns[2].spawn_safety_radius=120;
        cfg.patterns[3].wave_count=3;
    }
    if(argc>5)cfg.patterns[DEMO_PATTERN_COURSE].lane_spread_px=strtof(argv[5],NULL);
    char err[256];if(!demo_config_validate(&cfg,err,sizeof(err))){fprintf(stderr,"invalid config: %s\n",err);return 2;}
    FILE *f=fopen(output,"wb");if(!f)return 3;
    fprintf(f,"{\n\"config_version\":%u,\"candidate\":%u,\"held_out\":%s,\"tick_budget\":%d,\"type\":\"finite_public_observation_scripts_not_human_or_ml\",\"student_fire_min_range\":%.9g,\"parameters\":[",cfg.version,candidate,heldout?"true":"false",RUN_TICKS,cfg.student_fire_min_range);
    for(unsigned p=0;p<4;++p){const PatternConfig *pc=&cfg.patterns[p];
        fprintf(f,"%s{\"pattern\":%u,\"cost\":%d,\"windup\":%d,\"active\":%d,\"speed\":%.9g,\"waves\":%d,\"shots_per_wave\":%d,\"interval_sec\":%.9g,\"lane_spread_px\":%.9g,\"spawn_safety_radius\":%.9g}",p?",":"",p,pc->cost,pc->windup_ticks,pc->active_ticks,pc->bullet_speed,pc->wave_count,pc->shots_per_wave,pc->wave_interval_sec,pc->lane_spread_px,pc->spawn_safety_radius);
    }
    fprintf(f,"],\"results\":[\n");
    clock_t begin=clock();unsigned count=0;bool cpu_exhausted=false;
    unsigned course_only=argc>4?(unsigned)atoi(argv[4]):0;
    for(unsigned si=0;si<6&&!cpu_exhausted;++si)for(unsigned pop=0;pop<2&&!cpu_exhausted;++pop)
        for(unsigned mv=0;mv<4&&!cpu_exhausted;++mv)for(unsigned at=0;at<6;++at) {
            if(course_only && at!=DEMO_PATTERN_COURSE && at!=4)continue;
            Result r=run(&cfg,seeds[si],pop?8:3,mv,at);write_result(f,&r,count++>0);
            if((double)(clock()-begin)/CLOCKS_PER_SEC>=CPU_LIMIT){cpu_exhausted=true;break;}
        }
    double seconds=(double)(clock()-begin)/CLOCKS_PER_SEC;
    fprintf(f,"\n],\"runs\":%u,\"cpu_seconds\":%.3f,\"cpu_budget_exhausted\":%s}\n",count,seconds,cpu_exhausted?"true":"false");
    if(fclose(f))return 3;
    printf("balance runs=%u cpu_seconds=%.3f candidate=%u heldout=%u\n",count,seconds,candidate,heldout);
    return 0;
}
