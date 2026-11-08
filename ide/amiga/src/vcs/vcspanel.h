#ifndef AIDE_VCSPANEL_H
#define AIDE_VCSPANEL_H

/* Version control in the MUI IDE: AmiVCS as an external tool (amivcs),
 * see docs/AMIVCS-PLAN.md. Job building and result parsing are core's
 * (aide_vcs.c); this module runs amivcs asynchronously (SystemTagList
 * with NP_CurrentDir, its own timer.device tick polling the result file)
 * and owns the "Versionen" and "Verlauf" register pages and the
 * "Versionen" menu. mainwindow.c only hosts it (VcsHost). */

#include <exec/types.h>
#include <libraries/mui.h>

#include "aide/aide_project.h"

/* menu ids (MenuData[] in mainwindow.c) and gadget ids, all handled by
 * vcspanel_handle_id() */
#define ID_VCS_FIRST     4000
#define ID_VCS_INIT      4000
#define ID_VCS_REFRESH   4001
#define ID_VCS_STAGE_ALL 4002
#define ID_VCS_NEWBRANCH 4003
#define ID_VCS_SWITCH    4004
#define ID_VCS_MERGE     4005
#define ID_VCS_RESTORE   4006
#define ID_VCS_PULL      4007
#define ID_VCS_PUSH      4008
#define ID_VCS_CLONE     4009
#define ID_VCS_STAGE     4010
#define ID_VCS_UNSTAGE   4011
#define ID_VCS_COMMIT    4012
#define ID_VCS_SETTINGS  4013   /* AmiVCSConfig on the project */
#define ID_VCS_MERGE_ABORT 4014
#define ID_VCS_TAKE_OURS   4015 /* selected conflicts of the change list */
#define ID_VCS_TAKE_THEIRS 4016
#define ID_VCS_DIFF        4017 /* selected file -> output window */
#define ID_VCS_LAST      4019

typedef struct VcsHost {
    int  (*building)(void);                 /* a build/run is in progress */
    int  (*save_all)(void);                 /* editor file + project; 0 on failure */
    void (*reload_project)(void);           /* after amivcs rewrote files */
    void (*open_project)(const char *aproj);/* after a clone */
    void (*set_status)(const char *text);
    void (*append_output)(const char *text);/* one or more lines, '\n'-terminated */
    int  (*ask_string)(const char *title, const char *prompt, char *out, unsigned long outsz);
    int  (*ask_dir)(const char *title, char *out, unsigned long outsz);
    void (*set_locked)(int locked, int touchesFiles); /* editor read-only, build menus */
    /* user, password (hidden) and "remember" for a remote URL; `user`
     * holds the initial user; refused: the last try was turned down */
    int  (*ask_login)(const char *url, int refused, char *user, unsigned long usz,
                      char *pass, unsigned long psz, int *remember);
} VcsHost;

/* Before the application object exists: the two register pages. */
Object *vcspanel_create_page(void);
Object *vcspanel_create_history_page(void);

/* After it exists: notifications, timer, tool check. */
int   vcspanel_init(Object *app, Object *window, const VcsHost *host);
void  vcspanel_cleanup(void);
ULONG vcspanel_signal_mask(void);
void  vcspanel_handle_signals(ULONG got);

/* Project opened/closed/saved for the first time (root "" = none). */
void  vcspanel_project_changed(const char *projectRoot, const char *aprojPath);
/* A file was saved: status again, unless a job is running. */
void  vcspanel_file_saved(void);
/* Menu/gadget ids; returns 1 if it was one of ours. */
int   vcspanel_handle_id(long id);
/* 1 while amivcs is changing the repository (no build, no saving) */
int   vcspanel_writing(void);
/* the amivcs program (prefswindow.c: AmigaIDE/amivcs, default "amivcs") */
void  vcspanel_tool_changed(void);

#endif
