#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <GL/glut.h>
#include "common.h"
#include "shared_state.h"

/*
 * Layout:
 *   Top:    server info banner (port, version, stats)
 *   Middle: client list (one row per client, with progress bar)
 *   Bottom: recent log lines
 */

static int g_W = 860, g_H = 640;

/* ── Draw helpers ── */
static void quad(float x,float y,float w,float h){
    glBegin(GL_QUADS);
    glVertex2f(x,y);glVertex2f(x+w,y);
    glVertex2f(x+w,y+h);glVertex2f(x,y+h);
    glEnd();
}
static void str12(float x,float y,const char*s){
    glRasterPos2f(x,y);
    for(;*s;s++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12,(int)*s);
}
static void str18(float x,float y,const char*s){
    glRasterPos2f(x,y);
    for(;*s;s++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18,(int)*s);
}

/* Status colours */
static void set_status_color(int status){
    switch(status){
        case STATUS_CONNECTING:   glColor3f(0.65f,0.65f,0.70f); break;
        case STATUS_CHECKING:     glColor3f(0.90f,0.78f,0.20f); break;
        case STATUS_DOWNLOADING:  glColor3f(0.20f,0.65f,0.95f); break;
        case STATUS_DONE:         glColor3f(0.20f,0.88f,0.38f); break;
        case STATUS_UP_TO_DATE:   glColor3f(0.55f,0.88f,0.55f); break;
        case STATUS_ERROR:        glColor3f(0.95f,0.20f,0.20f); break;
        default:                  glColor3f(0.45f,0.45f,0.50f); break;
    }
}

/* ── Draw one client row ── */
static void draw_client_row(float x, float y, float w, const ClientSlot *c) {
    float h = 42.f;
    float rw = w - 20.f;

    /* Row background */
    glColor3f(0.12f,0.13f,0.16f);
    quad(x,y,rw,h);

    /* Status bar on the left */
    set_status_color(c->status);
    quad(x,y,4.f,h);

    /* IP + port */
    glColor3f(0.82f,0.82f,0.88f);
    char addr[64];
    snprintf(addr,sizeof addr,"%s:%d",c->ip,c->port);
    str12(x+12,y+h-10,addr);

    /* Version info */
    glColor3f(0.55f,0.55f,0.62f);
    char vbuf[48];
    if(c->client_version > 0)
        snprintf(vbuf,sizeof vbuf,"v%d",c->client_version);
    else
        snprintf(vbuf,sizeof vbuf,"v?");
    str12(x+160,y+h-10,vbuf);

    /* Status label */
    set_status_color(c->status);
    str12(x+210,y+h-10,STATUS_NAME[c->status]);

    /* Progress bar (only when downloading) */
    if(c->status==STATUS_DOWNLOADING && c->file_size>0){
        float frac=(float)c->bytes_sent/(float)c->file_size;
        float bx=x+320, by=y+h*0.35f, bw=rw-340.f, bh=10.f;
        glColor3f(0.14f,0.14f,0.20f); quad(bx,by,bw,bh);
        glColor3f(0.20f,0.65f,0.95f); quad(bx,by,bw*frac,bh);
        char pct[16]; snprintf(pct,sizeof pct,"%d%%",(int)(frac*100));
        glColor3f(0.75f,0.75f,0.80f);
        str12(bx+bw+6,by,pct);
    }

    /* File size when done */
    if(c->status==STATUS_DONE||c->status==STATUS_UP_TO_DATE){
        char sz[40];
        if(c->file_size>0)
            snprintf(sz,sizeof sz,"%ld KB",(c->file_size/1024));
        else
            snprintf(sz,sizeof sz,"—");
        glColor3f(0.45f,0.45f,0.52f);
        str12(x+320,y+h-10,sz);
    }

    /* Elapsed time */
    if(c->connect_time > 0){
        time_t elapsed;
        if(c->finish_time>0) elapsed = c->finish_time - c->connect_time;
        else                  elapsed = time(NULL)     - c->connect_time;
        char tbuf[20]; snprintf(tbuf,sizeof tbuf,"%lds",(long)elapsed);
        glColor3f(0.40f,0.40f,0.46f);
        str12(x+rw-52,y+h-10,tbuf);
    }

    /* Thread ID (short) */
    char tidbuf[24];
    snprintf(tidbuf,sizeof tidbuf,"tid:%lu",
             (unsigned long)c->thread_id % 10000);
    glColor3f(0.30f,0.30f,0.36f);
    str12(x+12,y+8,tidbuf);
}

/* ── Main display callback ── */
static void on_display(void){
    g_W=glutGet(GLUT_WINDOW_WIDTH);
    g_H=glutGet(GLUT_WINDOW_HEIGHT);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glOrtho(0,g_W,0,g_H,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();

    /* Background */
    glColor3f(0.08f,0.09f,0.11f); quad(0,0,g_W,g_H);

    /* ── Header banner ── */
    glColor3f(0.10f,0.16f,0.24f);
    quad(0,(float)g_H-64,(float)g_W,64);

    glColor3f(0.88f,0.88f,0.94f);
    str18(20,(float)g_H-24,"Software Update Server");

    /* Read state snapshot (no lock for display — eventual consistency) */
    int running        = g_state.running;
    int port           = g_state.port;
    int latest_version = g_state.latest_version;
    int total          = g_state.total_connected;
    int active         = g_state.active_count;
    int updates_sent   = g_state.updates_sent;
    int up_to_date     = g_state.up_to_date_count;

    char statbuf[128];
    snprintf(statbuf,sizeof statbuf,
             "Port: %d  |  Latest: v%d  |  Active: %d  |  "
             "Total: %d  |  Updates sent: %d  |  Up-to-date: %d",
             port,latest_version,active,total,updates_sent,up_to_date);
    glColor3f(0.50f,0.60f,0.72f);
    str12(20,(float)g_H-48,statbuf);

    /* Server status indicator */
    if(running){ glColor3f(0.20f,0.88f,0.38f); }
    else       { glColor3f(0.95f,0.20f,0.20f); }
    glBegin(GL_TRIANGLE_FAN);
    float cx=(float)g_W-24,cy=(float)g_H-32,r=7;
    glVertex2f(cx,cy);
    for(int i=0;i<=16;i++){
        float a=(float)i*2.f*3.14159f/16.f;
        glVertex2f(cx+r*cosf(a),cy+r*sinf(a));
    }
    glEnd();
    glColor3f(0.55f,0.55f,0.60f);
    str12((float)g_W-84,(float)g_H-28,running?"ONLINE":"OFFLINE");

    /* ── Column headers ── */
    float hy = (float)g_H - 84.f;
    glColor3f(0.35f,0.35f,0.42f);
    str12(30,  hy,"Address");
    str12(190, hy,"Ver");
    str12(240, hy,"Status");
    str12(350, hy,"Progress / Info");
    str12((float)g_W-100,hy,"Time");

    glColor3f(0.18f,0.18f,0.22f);
    glLineWidth(1.f);
    glBegin(GL_LINES);
    glVertex2f(20,hy-4); glVertex2f((float)g_W-20,hy-4);
    glEnd();

    /* ── Client rows ── */
    float row_y = (float)g_H - 96.f;
    float row_h = 46.f;
    int shown   = 0;

    for(int i=MAX_CLIENTS-1; i>=0 && row_y > 120.f; i--){
        if(!g_state.clients[i].active) continue;
        draw_client_row(20.f, row_y - row_h, (float)g_W - 40.f,
                        &g_state.clients[i]);
        row_y -= row_h + 4.f;
        shown++;
    }

    if(shown == 0){
        glColor3f(0.28f,0.28f,0.34f);
        str18((float)g_W*0.5f-120,(float)g_H*0.5f,
              "Waiting for clients...");
    }

    /* ── Log panel at the bottom ── */
    float log_h = 110.f;
    glColor3f(0.07f,0.08f,0.10f);
    quad(0,0,(float)g_W,log_h);
    glColor3f(0.14f,0.14f,0.18f);
    glLineWidth(1.f);
    glBegin(GL_LINES);
    glVertex2f(0,log_h); glVertex2f((float)g_W,log_h);
    glEnd();

    glColor3f(0.35f,0.35f,0.42f);
    str12(12,log_h-12,"Recent events:");

    /* Show log lines newest-first */
    float ly = log_h - 30.f;
    int   head = g_state.log_head;
    for(int i=0; i<LOG_DISPLAY_LINES && ly > 4.f; i++){
        int idx=(head - 1 - i + LOG_DISPLAY_LINES) % LOG_DISPLAY_LINES;
        if(!g_state.log_lines[idx][0]) { ly-=14.f; continue; }
        glColor3f(0.45f+0.05f*i,0.50f+0.02f*i,0.55f+0.02f*i);
        str12(12,ly,g_state.log_lines[idx]);
        ly -= 14.f;
    }

    glutSwapBuffers();
}

static void on_timer(int v){
    (void)v;
    glutPostRedisplay();
    glutTimerFunc(16,on_timer,0);
}

/* ── Display thread entry ── */
void *display_thread(void *arg){
    (void)arg;
    /* Check if a display is available — skip gracefully if not */
    const char *disp = getenv("DISPLAY");
    if (!disp || !disp[0]) {
        fprintf(stderr,"[Display] No DISPLAY - skipping OpenGL window\n");
        return NULL;
    }
    int argc=0;
    glutInit(&argc,NULL);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGB);
    glutInitWindowSize(g_W,g_H);
    glutInitWindowPosition(60,30);
    glutCreateWindow("Software Update Server — Dashboard");
    glClearColor(0.08f,0.09f,0.11f,1.f);
    glutDisplayFunc(on_display);
    glutTimerFunc(16,on_timer,0);
    glutMainLoop();
    return NULL;
}
