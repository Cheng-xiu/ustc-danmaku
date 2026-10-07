/* v5 skill roles: validate distinct real geometry, immutable plans, and safety.
 * These fixtures use actual pattern emitters rather than injected projectiles. */
#include "demo_base.h"
#include "pattern_ring.h"
#include "pattern_course.h"
#include "pattern_mine.h"
#include "pattern_shower.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"line %d: %s\n",__LINE__,#x); } } while(0)
static bool near(float a, float b) { return fabsf(a-b) < .002f; }
static PatternRequest request(void) {
    PatternRequest r; memset(&r,0,sizeof(r));
    r.origin_x=480; r.origin_y=620; r.target_x=480; r.target_y=430;
    r.target_id=2; r.start_tick=971; r.student_count=1; r.student_radius=20;
    r.student_x[0]=r.target_x; r.student_y[0]=r.target_y; r.student_alive[0]=true;
    r.field_w=960; r.field_h=720; return r;
}
static void test_local_ring(const DemoConfig *base) {
    DemoConfig cfg=*base; PatternRequest r=request(); AttackPlan p; Rng rng;
    rng_seed(&rng,42,0); r.pattern=DEMO_PATTERN_RING;
    CHECK(pattern_ring_make_plan(&r,&cfg,&rng,&p));
    CHECK(p.windup_ticks < cfg.patterns[DEMO_PATTERN_COURSE].windup_ticks);
    CHECK(p.lock_speed*p.active_ticks/60.0f < cfg.field_w);
    CHECK(p.wave_count==2 && p.gap_drift_deg_per_wave < p.gap_span_deg);
    cfg.patterns[DEMO_PATTERN_RING].bullet_speed=1;
    cfg.patterns[DEMO_PATTERN_RING].gap_span_deg=359;
    unsigned total=0;
    for (int wave=0;wave<p.wave_count;++wave) {
        ProjectileSpawnBuffer b; spawn_buffer_init(&b);
        CHECK(pattern_ring_emit(&p,&cfg,(unsigned)lroundf(p.wave_tick[wave]),&b));
        CHECK(b.count==24); total+=b.count;
        for(unsigned k=0;k<b.count;++k) {
            CHECK(near(b.spec[k].x,r.origin_x) && near(b.spec[k].y,r.origin_y));
            CHECK(near(hypotf(b.spec[k].vx,b.spec[k].vy),p.lock_speed));
        }
    }
    CHECK(total==48);
}
static void test_column_walls(const DemoConfig *base) {
    DemoConfig cfg=*base; PatternRequest r=request(); AttackPlan p; Rng rng;
    rng_seed(&rng,42,0); r.pattern=DEMO_PATTERN_COURSE;
    CHECK(pattern_course_make_plan(&r,&cfg,&rng,&p));
    CHECK(p.wave_count==2 && p.shots_per_wave==24);
    CHECK(p.lock_speed < cfg.patterns[DEMO_PATTERN_SHOWER].bullet_speed);
    cfg.patterns[DEMO_PATTERN_COURSE].lane_spread_px=0;
    cfg.patterns[DEMO_PATTERN_COURSE].shots_per_wave=3;
    cfg.patterns[DEMO_PATTERN_COURSE].wave_count=8;
    for(int wave=0;wave<p.wave_count;++wave) {
        ProjectileSpawnBuffer b; spawn_buffer_init(&b);
        CHECK(pattern_course_emit(&p,&cfg,(unsigned)lroundf(p.wave_tick[wave]),&b));
        CHECK(b.count==24);
        int channel=((int)p.wave_offset+wave)%3;
        for(unsigned k=0;k<b.count;++k) {
            const Projectile *s=&b.spec[k]; int col=(int)(s->x/320);
            CHECK(col>=0 && col<3 && col!=channel);
            CHECK(near(s->y,100) && near(s->vy,p.lock_speed) && near(s->vx,0));
            CHECK(s->x-s->radius>=col*320 && s->x+s->radius<=(col+1)*320);
            for(unsigned j=0;j<k;++j) CHECK(!near(s->x,b.spec[j].x));
        }
        CHECK(near(b.spec[11].x-b.spec[0].x,280));
        CHECK(near(b.spec[23].x-b.spec[12].x,280));
    }
}
static void test_target_fan(const DemoConfig *base) {
    DemoConfig cfg=*base; PatternRequest r=request(); AttackPlan p; Rng rng;
    rng_seed(&rng,42,0); r.pattern=DEMO_PATTERN_MINE;
    CHECK(pattern_mine_make_plan(&r,&cfg,&rng,&p));
    CHECK(p.wave_count==3 && near(p.wave_offset,1));
    CHECK(near(hypotf(p.origin_x-r.target_x,p.origin_y-r.target_y),120));
    CHECK(p.gap_span_deg==40 && p.windup_ticks==18);
    cfg.patterns[DEMO_PATTERN_MINE].gap_span_deg=170;
    cfg.patterns[DEMO_PATTERN_MINE].shots_per_wave=31;
    cfg.patterns[DEMO_PATTERN_MINE].wave_interval_sec=1;
    cfg.patterns[DEMO_PATTERN_MINE].bullet_speed=100;
    unsigned bursts=0,total=0;
    for(unsigned tick=0;tick<=(unsigned)p.active_ticks;++tick) {
        ProjectileSpawnBuffer b; spawn_buffer_init(&b);
        bool emitted=pattern_mine_emit(&p,&cfg,tick,&b);
        CHECK(emitted == (tick==0 || tick==18 || tick==36));
        if(!emitted) { CHECK(b.count==0); continue; }
        ++bursts; total+=b.count; CHECK(b.count==9);
        for(unsigned k=0;k<b.count;++k) {
            const Projectile *s=&b.spec[k];
            CHECK(near(s->x,p.origin_x) && near(s->y,p.origin_y));
            CHECK(near(hypotf(s->vx,s->vy),480));
            CHECK(s->vy/480 >= cosf(20*3.14159265f/180)-.0001f);
        }
    }
    CHECK(bursts==3 && total==27);
    PatternWarning warning; pattern_mine_warning(&p,&cfg,&warning);
    CHECK(warning.valid && warning.gap_span_deg==40 && warning.wave_count==3);
    cfg=*base; r.student_count=2; r.student_alive[1]=true;
    r.student_x[1]=p.origin_x; r.student_y[1]=p.origin_y;
    AttackPlan sentinel,untouched; memset(&sentinel,0xa5,sizeof(sentinel)); untouched=sentinel;
    CHECK(!pattern_mine_make_plan(&r,&cfg,&rng,&sentinel));
    CHECK(memcmp(&sentinel,&untouched,sizeof(sentinel))==0);
    r.student_alive[1]=false; cfg.patterns[DEMO_PATTERN_MINE].gap_span_deg=NAN;
    CHECK(!pattern_mine_make_plan(&r,&cfg,&rng,&sentinel));
    CHECK(memcmp(&sentinel,&untouched,sizeof(sentinel))==0);
}
static void test_sweeping_rain(const DemoConfig *cfg) {
    PatternRequest r=request(); AttackPlan p; Rng rng; rng_seed(&rng,42,0);
    r.pattern=DEMO_PATTERN_SHOWER; memset(&p,0,sizeof(p));
    CHECK(pattern_shower_make_plan(&r,cfg,&rng,&p));
    CHECK(cfg->patterns[DEMO_PATTERN_SHOWER].cost==100);
    CHECK(p.wave_count==5 && p.active_ticks>cfg->patterns[DEMO_PATTERN_COURSE].active_ticks);
    ProjectileSpawnBuffer b; spawn_buffer_init(&b);
    CHECK(pattern_shower_emit(&p,cfg,0,&b) && b.count==16);
    bool columns[3]={false,false,false};
    for(unsigned i=0;i<b.count;++i) {
        columns[(unsigned)(b.spec[i].x/320)]=true;
        CHECK(near(b.spec[i].y,100));
    }
    CHECK(columns[0] && columns[1] && columns[2]);
}
int main(void) {
    DemoConfig cfg; CHECK(demo_config_init(&cfg)); CHECK(cfg.version==5);
    char error[256]; CHECK(demo_config_validate(&cfg,error,sizeof(error)));
    test_local_ring(&cfg); test_column_walls(&cfg); test_target_fan(&cfg); test_sweeping_rain(&cfg);
    printf("v5 pattern roles: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
