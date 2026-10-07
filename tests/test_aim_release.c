/* Pure preview, release transaction and world integration; fixture HP/speed only. */
#include "world.h"
#include "attack.h"
#include "patterns.h"
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static World w, saved;
static unsigned checks, failures;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while(0)
static void fixture(void) {
    DemoConfig c; CHECK(demo_config_init(&c));
    c.boss_hp = c.student_hp = 100000;
    c.student_speed = .001f; c.student_fire_interval_ticks = 100000;
    c.energy_start = c.energy_max; c.energy_regen_per_sec = 0; c.max_ticks = 0;
    CHECK(world_reset(&w,&c,12345));
    w.boss.x=480; w.boss.y=500;
    for(unsigned i=0;i<w.student_count;i++) { w.students[i].x=150+i*150; w.students[i].y=150; }
}
int main(void) {
    for(unsigned p=0;p<4;p++) for(unsigned d=0;d<8;d++) {
        fixture();
        float dx=cosf((float)d*0.7853981634f), dy=sinf((float)d*0.7853981634f);
        AttackPlan preview; memset(&preview,0,sizeof(preview));
        DemoRejectReason reason;
        saved=w;
        for(unsigned repeat=0;repeat<10;repeat++) {
            CHECK(attack_build_aim_plan(&w,(DemoPattern)p,dx,dy,&preview,&reason));
            CHECK(reason==DEMO_REJECT_NONE); CHECK(memcmp(&w,&saved,sizeof(w))==0);
        }
        CHECK(preview.manual_aim && preview.target_id==0 && preview.origin_x==480 && preview.origin_y==500);
        int energy=w.energy;
        CHECK(attack_try_request_aim(&w,(DemoPattern)p,dx,dy,&reason));
        CHECK(reason==DEMO_REJECT_NONE && w.energy==energy-w.cfg.patterns[p].cost);
        CHECK(w.attack_state==DEMO_ATTACK_WINDUP && w.plan.plan_id==1 && w.next_plan_id==2);
        preview.plan_id=w.plan.plan_id; preview.active=w.plan.active;
        CHECK(memcmp(&preview,&w.plan,sizeof(preview))==0);
        saved=w;
        CHECK(!attack_try_request_aim(&w,(DemoPattern)p,dx,dy,&reason));
        CHECK(reason==DEMO_REJECT_BUSY && memcmp(&w,&saved,sizeof(w))==0);
        CHECK(attack_build_aim_plan(&w,(DemoPattern)p,dx,dy,&preview,&reason));
        CHECK(reason==DEMO_REJECT_BUSY && memcmp(&w,&saved,sizeof(w))==0);
        BossInput input={0};
        unsigned births=0;
        const unsigned end=w.plan.windup_ticks+w.plan.active_ticks;
        for(unsigned tick=0;tick<=end;tick++) {
            world_step(&w,&input);
            for(unsigned ev=0;ev<w.events.count;ev++) if(w.events.items[ev].type==DEMO_EVENT_WAVE_SPAWN) births+=(unsigned)w.events.items[ev].amount;
        }
        CHECK(births==(unsigned)(w.plan.wave_count*w.plan.shots_per_wave));
        CHECK(w.energy==energy-w.cfg.patterns[p].cost && w.attack_accept_count==0);
        CHECK(w.attack_state==DEMO_ATTACK_IDLE);
        fixture(); w.energy=0; saved=w;
        CHECK(attack_build_aim_plan(&w,(DemoPattern)p,dx,dy,&preview,&reason));
        CHECK(reason==DEMO_REJECT_NO_ENERGY && memcmp(&w,&saved,sizeof(w))==0);
        CHECK(!attack_try_request_aim(&w,(DemoPattern)p,dx,dy,&reason));
        CHECK(reason==DEMO_REJECT_NO_ENERGY && memcmp(&w,&saved,sizeof(w))==0);
        fixture();
        BossInput release={0}; release.manual_aim=true; release.aim_dir_x=dx; release.aim_dir_y=dy;
        release.move_x=1; release.attack_requested[p]=true;
        world_step(&w,&release);
        CHECK(w.attack_accept_count==1 && w.plan.origin_x==480 && w.boss.x>480 && w.plan.manual_aim);
        CHECK(fabsf(w.plan.aim_dir_x-dx)<1e-5f && fabsf(w.plan.aim_dir_y-dy)<1e-5f);
        release.attack_requested[p]=false; world_step(&w,&release); CHECK(w.attack_accept_count==1);
    }
    fixture(); AttackPlan plan; DemoRejectReason reason;
    saved=w;
    CHECK(!attack_build_aim_plan(&w,DEMO_PATTERN_MINE,0,0,&plan,&reason));
    CHECK(!attack_build_aim_plan(&w,DEMO_PATTERN_MINE,NAN,1,&plan,&reason));
    CHECK(attack_build_aim_plan(&w,DEMO_PATTERN_MINE,FLT_MAX,FLT_MAX,&plan,&reason));
    CHECK(memcmp(&w,&saved,sizeof(w))==0);
    w.students[0].x=w.boss.x+119; w.students[0].y=w.boss.y; saved=w;
    CHECK(!attack_build_aim_plan(&w,DEMO_PATTERN_MINE,1,0,&plan,&reason));
    CHECK(reason==DEMO_REJECT_NONE && memcmp(&w,&saved,sizeof(w))==0);
    CHECK(!attack_try_request_aim(&w,DEMO_PATTERN_MINE,1,0,&reason));
    CHECK(memcmp(&w,&saved,sizeof(w))==0);
    for(unsigned i=0;i<w.student_count;i++) w.students[i].alive=false;
    saved=w;
    CHECK(attack_build_aim_plan(&w,DEMO_PATTERN_RING,1,0,&plan,&reason));
    CHECK(reason==DEMO_REJECT_NO_TARGET && memcmp(&w,&saved,sizeof(w))==0);
    CHECK(!attack_try_request_aim(&w,DEMO_PATTERN_RING,1,0,&reason));
    CHECK(memcmp(&w,&saved,sizeof(w))==0);
    printf("aim release: %u checks, %u failures\n",checks,failures);
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
