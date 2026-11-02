/* vcspanel.c -- see vcs/vcspanel.h */
#include "vcs/vcspanel.h"
#include "app/prefswindow.h"

#include <clib/alib_protos.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "mui/mui_stubs.h"
#include "mui/NList_mcc.h"
#include "mui/NListview_mcc.h"

#include "aide/aide_fs.h"
#include "aide/aide_str.h"
#include "aide/aide_vcs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TMP_BASE "T:aide-vcs"

static Object *g_app, *g_window;
static VcsHost g_host;

static Object *g_page, *g_info, *g_branch, *g_unstaged, *g_staged, *g_message, *g_all;
static Object *g_initBtn, *g_refreshBtn, *g_stageBtn, *g_stageAllBtn, *g_unstageBtn, *g_commitBtn;
static Object *g_historyList;

static char g_root[256];      /* project root, "" if none */
static char g_aproj[256];
static int  g_available;      /* amivcs found and new enough */
static int  g_checked;        /* tool check done (or running) */
static int  g_repo;           /* .amivcs in g_root */
static AideVcsResult g_status;
static int  g_haveStatus;

/* ------------------------------------------------------------------ */
/* job runner: one job at a time, a queue of reading jobs behind it     */

typedef struct Pending {
    AideVcsOp op;
    AideStrList items;
    char *message;
    int flag;
    char workDir[256];
} Pending;

static struct {
    int        running;
    AideVcsOp  op;
    AideVcsJob job;
    long       started;     /* time() of the launch */
    char       cloneTarget[256];
} g_run;

static Pending g_queue[8];
static int g_queued;

static struct MsgPort *g_timerPort;
static struct timerequest *g_timerReq;
static ULONG g_timerSig;
static int g_timerOpen, g_timerArmed;

static AideVcsOp g_pendingBranchOp = AIDE_VCS_SWITCH;

static void timer_arm(void)
{
    if (!g_timerOpen || g_timerArmed) return;
    g_timerReq->tr_node.io_Command = TR_ADDREQUEST;
    g_timerReq->tr_time.tv_secs = 0;
    g_timerReq->tr_time.tv_micro = 250000;
    SendIO((struct IORequest *)g_timerReq);
    g_timerArmed = 1;
}

static void status(const char *t) { if (g_host.set_status) g_host.set_status(t); }
static void output(const char *t) { if (g_host.append_output) g_host.append_output(t); }

static void set_enabled(int on)
{
    Object *o[6];
    int i;
    o[0] = g_stageBtn; o[1] = g_stageAllBtn; o[2] = g_unstageBtn; o[3] = g_commitBtn;
    o[4] = g_refreshBtn; o[5] = g_initBtn;
    for (i = 0; i < 6; i++)
        if (o[i]) set(o[i], MUIA_Disabled, on ? FALSE : TRUE);
    if (on && g_initBtn) set(g_initBtn, MUIA_Disabled, (g_root[0] && g_available && !g_repo) ? FALSE : TRUE);
    if (on && g_repo == 0) {
        for (i = 0; i < 5; i++) if (o[i]) set(o[i], MUIA_Disabled, TRUE);
    }
}

static int launch(Pending *p)
{
    BPTR nilIn, nilOut, cwd;
    struct TagItem tags[6];
    char *cmd;
    LONG rc;

    if (!aide_vcs_job_build(&g_run.job, p->op, prefs_amivcs_program(), TMP_BASE, &p->items,
                            p->message ? p->message : "", p->flag, 100)) {
        aide_vcs_job_free(&g_run.job);
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Konnte die Auftragsdateien in T: nicht schreiben.");
        return 0;
    }
    remove(g_run.job.resultPath);   /* an old result must not count */
    cmd = aide_vcs_job_cmdline(&g_run.job);
    nilIn = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    nilOut = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    cwd = Lock((CONST_STRPTR)(p->workDir[0] ? p->workDir : (g_root[0] ? g_root : "T:")), ACCESS_READ);

    tags[0].ti_Tag = SYS_Input;     tags[0].ti_Data = (ULONG)nilIn;
    tags[1].ti_Tag = SYS_Output;    tags[1].ti_Data = (ULONG)nilOut;
    tags[2].ti_Tag = SYS_Asynch;    tags[2].ti_Data = TRUE;
    tags[3].ti_Tag = NP_CurrentDir; tags[3].ti_Data = (ULONG)cwd;
    tags[4].ti_Tag = NP_StackSize;  tags[4].ti_Data = 65536;
    tags[5].ti_Tag = TAG_DONE;      tags[5].ti_Data = 0;
    rc = SystemTagList((CONST_STRPTR)cmd, tags);
    free(cmd);
    if (rc == -1) {
        if (nilIn) Close(nilIn);
        if (nilOut) Close(nilOut);
        if (cwd) UnLock(cwd);
        aide_vcs_job_free(&g_run.job);
        return 0;
    }
    g_run.running = 1;
    g_run.op = p->op;
    g_run.started = (long)time(NULL);
    if (aide_vcs_op_writes(p->op)) {
        set_enabled(0);
        if (g_host.set_locked) g_host.set_locked(1, aide_vcs_op_touches_files(p->op));
    }
    timer_arm();
    return 1;
}

static void pending_free(Pending *p)
{
    aide_strlist_free(&p->items);
    free(p->message);
    p->message = NULL;
}

static void start_next(void);

/* queue or start a job; writes are refused while anything else runs */
static int run_job(AideVcsOp op, const AideStrList *items, const char *message, int flag, const char *workDir)
{
    Pending p;
    size_t i;
    int writes = aide_vcs_op_writes(op);

    if (writes && (g_run.running || g_queued)) {
        status("Ein Versionsverwaltungs-Auftrag l\344uft noch.");
        return 0;
    }
    if (writes && g_host.building && g_host.building()) {
        status("Ein Build l\344uft -- die Versionsverwaltung muss warten.");
        return 0;
    }
    memset(&p, 0, sizeof(p));
    p.op = op;
    aide_strlist_init(&p.items);
    for (i = 0; items && i < items->count; i++) aide_strlist_push(&p.items, items->items[i]);
    p.message = message ? aide_strdup(message) : NULL;
    p.flag = flag;
    if (workDir) strncpy(p.workDir, workDir, sizeof(p.workDir) - 1);

    if (g_run.running) {
        if (g_queued >= (int)(sizeof(g_queue) / sizeof(g_queue[0]))) { pending_free(&p); return 0; }
        g_queue[g_queued++] = p;
        return 1;
    }
    if (!launch(&p)) {
        pending_free(&p);
        if (op == AIDE_VCS_VERSION) {
            g_available = 0;
            set(g_info, MUIA_Text_Contents,
                "amivcs wurde nicht gefunden.\nDie Versionsverwaltung braucht AmiVCS\n"
                "(Programm unter Einstellungen eintragen).");
        } else {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "\"%s\" konnte nicht gestartet werden.", prefs_amivcs_program());
        }
        return 0;
    }
    pending_free(&p);
    return 1;
}

static int run_simple(AideVcsOp op)
{
    return run_job(op, NULL, NULL, 0, NULL);
}

static int run_one(AideVcsOp op, const char *item)
{
    AideStrList l;
    int ok;
    aide_strlist_init(&l);
    aide_strlist_push(&l, item);
    ok = run_job(op, &l, NULL, 0, NULL);
    aide_strlist_free(&l);
    return ok;
}

static void start_next(void)
{
    Pending p;
    int i;
    while (!g_run.running && g_queued > 0) {
        p = g_queue[0];
        for (i = 1; i < g_queued; i++) g_queue[i - 1] = g_queue[i];
        g_queued--;
        launch(&p);
        pending_free(&p);
    }
}

/* ------------------------------------------------------------------ */
/* results                                                              */

static void show_messages(const AideVcsResult *r)
{
    size_t i;
    AideStr s;
    aide_str_init(&s);
    for (i = 0; i < r->messageCount; i++) {
        aide_str_append(&s, r->messages[i].level == 2 ? "Fehler: " : r->messages[i].level == 1 ? "Warnung: " : "");
        aide_str_append(&s, r->messages[i].text);
        aide_str_append_char(&s, '\n');
    }
    if (s.len) output(s.data);
    aide_str_free(&s);
}

static const char *kind_text(char k)
{
    return k == 'N' ? "neu      " : k == 'D' ? "gel\366scht " : "ge\344ndert ";
}

static void show_status(const AideVcsResult *r)
{
    size_t i;
    char line[300];
    if (r->haveHead) {
        snprintf(line, sizeof(line), r->detached ? "Losgel\366st bei %.8s" : "Branch: %s", r->branch);
        set(g_branch, MUIA_Text_Contents, line);
    }
    set(g_unstaged, MUIA_NList_Quiet, TRUE);
    set(g_staged, MUIA_NList_Quiet, TRUE);
    DoMethod(g_unstaged, MUIM_NList_Clear);
    DoMethod(g_staged, MUIM_NList_Clear);
    for (i = 0; i < r->changeCount; i++) {
        snprintf(line, sizeof(line), "%s%s", kind_text(r->changes[i].kind), r->changes[i].path);
        DoMethod(r->changes[i].staged == 'S' ? g_staged : g_unstaged, MUIM_NList_InsertSingle,
                 (ULONG)line, MUIV_NList_Insert_Bottom);
    }
    set(g_unstaged, MUIA_NList_Quiet, FALSE);
    set(g_staged, MUIA_NList_Quiet, FALSE);
}

static void take_status(AideVcsResult *r)
{
    if (!r->haveHead) return;
    if (g_haveStatus) aide_vcs_result_free(&g_status);
    g_status = *r;                 /* takes ownership */
    aide_vcs_result_init(r);
    g_haveStatus = 1;
    show_status(&g_status);
}

static void show_log(const AideVcsResult *r)
{
    size_t i;
    char line[300];
    set(g_historyList, MUIA_NList_Quiet, TRUE);
    DoMethod(g_historyList, MUIM_NList_Clear);
    for (i = 0; i < r->commitCount; i++) {
        const AideVcsCommit *c = &r->commits[i];
        time_t t = (time_t)c->mtime;
        struct tm *tm = gmtime(&t);   /* commit times are UTC; the Amiga has no zone */
        char msg[160];
        const char *nl = strchr(c->message, '\n');
        size_t n = nl ? (size_t)(nl - c->message) : strlen(c->message);
        if (n >= sizeof(msg)) n = sizeof(msg) - 1;
        memcpy(msg, c->message, n);
        msg[n] = '\0';
        snprintf(line, sizeof(line), "%.8s  %04d-%02d-%02d %02d:%02d  %-12.12s  %s%s%s%s",
                 c->oid, tm ? tm->tm_year + 1900 : 0, tm ? tm->tm_mon + 1 : 0, tm ? tm->tm_mday : 0,
                 tm ? tm->tm_hour : 0, tm ? tm->tm_min : 0, c->author,
                 c->refs[0] ? "[" : "", c->refs, c->refs[0] ? "] " : "", msg);
        DoMethod(g_historyList, MUIM_NList_InsertSingle, (ULONG)line, MUIV_NList_Insert_Bottom);
    }
    set(g_historyList, MUIA_NList_Quiet, FALSE);
}

static void refresh(int withLog)
{
    if (!g_available || !g_repo) return;
    run_simple(AIDE_VCS_STATUS);
    if (withLog) run_simple(AIDE_VCS_LOG);
}

static void choose_branch(const AideVcsResult *r)
{
    AideStr prompt;
    char name[128];
    size_t i;
    aide_str_init(&prompt);
    aide_str_append(&prompt, g_pendingBranchOp == AIDE_VCS_MERGE ? "Welchen Branch zusammenf\374hren?\n\n"
                                                                 : "Zu welchem Branch wechseln?\n\n");
    for (i = 0; i < r->refs.count; i++) {
        aide_str_append(&prompt, (r->currentRef && strcmp(r->currentRef, r->refs.items[i]) == 0) ? "* " : "  ");
        aide_str_append(&prompt, r->refs.items[i]);
        aide_str_append_char(&prompt, '\n');
    }
    name[0] = '\0';
    if (g_host.ask_string && g_host.ask_string("Versionen", prompt.data, name, sizeof(name)) && name[0]) {
        if (g_host.save_all && !g_host.save_all()) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Nicht alles konnte gespeichert werden; nichts ge\344ndert.");
        } else {
            run_one(g_pendingBranchOp, name);
        }
    }
    aide_str_free(&prompt);
}

static void check_ignore(void)
{
    AideProject p;
    char *path, *text = NULL;
    size_t size = 0;
    AideStrList missing;

    if (!g_aproj[0]) return;
    aide_project_init(&p);
    if (!aide_project_load(&p, g_aproj)) { aide_project_free(&p); return; }
    path = aide_fs_join(g_root, ".amivcsignore");
    if (path) aide_fs_read_file(path, &text, &size);
    aide_strlist_init(&missing);
    aide_vcs_ignore_missing(&p, text, &missing);
    if (missing.count > 0) {
        AideStr list;
        size_t i;
        aide_str_init(&list);
        for (i = 0; i < missing.count; i++) { aide_str_append(&list, missing.items[i]); aide_str_append_char(&list, '\n'); }
        if (MUI_Request(g_app, g_window, 0, "AmigaIDE", "_Eintragen|_Nein",
                        "Build-Ausgaben und IDE-Dateien dieses Projekts sind\n"
                        "noch nicht von der Versionsverwaltung ausgenommen.\n"
                        "Diese Zeilen in .amivcsignore eintragen?\n\n%s", list.data) == 1)
            aide_vcs_ignore_append(g_root, &missing);
        aide_str_free(&list);
    }
    aide_strlist_free(&missing);
    free(text);
    free(path);
    aide_project_free(&p);
}

static void update_view(void)
{
    if (!g_available)
        set(g_info, MUIA_Text_Contents,
            g_checked ? "amivcs wurde nicht gefunden oder ist zu alt\n(AmiVCS ab 0.6, Programm unter Einstellungen)."
                      : "Pr\374fe amivcs...");
    else if (!g_root[0])
        set(g_info, MUIA_Text_Contents, "Kein Projekt ge\366ffnet.");
    else if (!g_repo)
        set(g_info, MUIA_Text_Contents, "Dieses Projekt steht noch nicht\nunter Versionsverwaltung.");
    else
        set(g_info, MUIA_Text_Contents, "");
    if (!g_repo || !g_available) {
        DoMethod(g_unstaged, MUIM_NList_Clear);
        DoMethod(g_staged, MUIM_NList_Clear);
        DoMethod(g_historyList, MUIM_NList_Clear);
        set(g_branch, MUIA_Text_Contents, "");
    }
    set_enabled(!g_run.running || !aide_vcs_op_writes(g_run.op));
}

static void job_done(void)
{
    AideVcsResult r;
    AideVcsOp op = g_run.op;
    int parsed, ok;

    aide_vcs_result_init(&r);
    parsed = aide_vcs_parse_file(&r, g_run.job.resultPath);
    ok = parsed && r.ended && r.rc == 0;
    aide_vcs_job_free(&g_run.job);
    g_run.running = 0;

    if (aide_vcs_op_writes(op) && g_host.set_locked) g_host.set_locked(0, 0);

    switch (op) {
    case AIDE_VCS_VERSION:
        g_available = parsed;
        g_checked = 1;
        update_view();
        if (g_available && g_root[0]) vcspanel_project_changed(g_root, g_aproj);
        break;
    case AIDE_VCS_STATUS:
        if (ok) take_status(&r);
        else show_messages(&r);
        break;
    case AIDE_VCS_LOG:
        if (ok) show_log(&r);
        break;
    case AIDE_VCS_BRANCHES:
        if (ok) choose_branch(&r);
        else show_messages(&r);
        break;
    default:
        if (op == AIDE_VCS_PUSH && !ok && aide_vcs_needs_remote_name(&r)) {
            /* a first push needs the repository name on the server */
            char name[128];
            const char *base = strrchr(g_root, '/');
            base = base ? base + 1 : (strchr(g_root, ':') ? strchr(g_root, ':') + 1 : g_root);
            strncpy(name, base, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            if (g_host.ask_string && g_host.ask_string("Push", "Dieses Repository wurde noch nie gepusht.\n"
                                                       "Name des Repositorys auf dem Server\n"
                                                       "(wird angelegt, wenn es fehlt):", name, sizeof(name))
                && name[0]) {
                aide_vcs_result_free(&r);
                set_enabled(1);
                run_one(AIDE_VCS_PUSH, name);
                return;
            }
        }
        show_messages(&r);
        if (!ok) {
            const char *e = aide_vcs_result_error(&r);
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "%s",
                        e ? e : (parsed ? "amivcs meldet einen Fehler." : "amivcs hat nicht wie erwartet geantwortet."));
        }
        if (op == AIDE_VCS_CLONE) {
            if (ok && g_host.open_project) {
                AideStrList found;
                aide_strlist_init(&found);
                aide_fs_list_files(g_run.cloneTarget, ".aproj", &found);
                if (found.count > 0) {   /* names, not paths (aide_fs.h) */
                    char *aproj = aide_fs_join(g_run.cloneTarget, found.items[0]);
                    if (aproj) g_host.open_project(aproj);
                    free(aproj);
                }
                else MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                                 "Geklont nach %s.\nDort liegt noch keine .aproj.", g_run.cloneTarget);
                aide_strlist_free(&found);
            }
            g_run.cloneTarget[0] = '\0';
            break;
        }
        if (op == AIDE_VCS_INIT) {
            g_repo = aide_vcs_present(g_root);
            update_view();
            if (g_repo) { check_ignore(); refresh(1); }
            break;
        }
        if (parsed) take_status(&r);
        if (op == AIDE_VCS_COMMIT && ok) {
            set(g_message, MUIA_String_Contents, "");
            set(g_all, MUIA_Selected, FALSE);
        }
        if (aide_vcs_op_touches_files(op) && g_host.reload_project) g_host.reload_project();
        if (op == AIDE_VCS_COMMIT || op == AIDE_VCS_BRANCH_NEW || aide_vcs_op_touches_files(op))
            run_simple(AIDE_VCS_LOG);
        if (ok) status("Versionsverwaltung: fertig");
        break;
    }
    aide_vcs_result_free(&r);
    set_enabled(1);
    start_next();
}

/* the end record is in the result file, or the job started but never
 * produced one (amivcs crashed or was not found) */
static void tick(void)
{
    char *data = NULL;
    size_t size = 0;
    if (!g_run.running) return;
    if (aide_fs_read_file(g_run.job.resultPath, &data, &size) && data) {
        int done = strstr(data, "\nend\t") != NULL;
        free(data);
        if (done) { job_done(); return; }
    } else if (!aide_vcs_op_network(g_run.op) && (long)time(NULL) - g_run.started > 120) {
        job_done();   /* gives up with "did not answer" */
        return;
    }
    timer_arm();
}

/* ------------------------------------------------------------------ */
/* UI                                                                   */

Object *vcspanel_create_page(void)
{
    g_info = TextObject, MUIA_Text_Contents, "", End;
    g_branch = TextObject, MUIA_Text_Contents, "", End;
    g_refreshBtn = SimpleButton("Aktualisieren");
    g_initBtn = SimpleButton("Anlegen");   /* Repository; kurz, die Spalte bleibt schmal */
    g_unstaged = NListObject,
        MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
        MUIA_NList_DestructHook,  MUIV_NList_DestructHook_String,
        MUIA_NList_MultiSelect,   MUIV_NList_MultiSelect_Default,
    End;
    g_staged = NListObject,
        MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
        MUIA_NList_DestructHook,  MUIV_NList_DestructHook_String,
        MUIA_NList_MultiSelect,   MUIV_NList_MultiSelect_Default,
    End;
    g_stageBtn = SimpleButton("Bereitstellen");
    g_stageAllBtn = SimpleButton("Alle");
    g_unstageBtn = SimpleButton("Zur\374cknehmen");
    g_message = StringObject, StringFrame, MUIA_String_MaxLen, 400, End;
    g_all = MUI_MakeObject(MUIO_Checkmark, (ULONG)"alle");
    g_commitBtn = SimpleButton("Commit");

    g_page = VGroup,
        Child, g_info,
        Child, g_branch,
        Child, HGroup, Child, g_refreshBtn, Child, g_initBtn, End,
        Child, TextObject, MUIA_Text_Contents, "Ge\344ndert:", End,
        Child, NListviewObject, MUIA_NListview_NList, g_unstaged, End,
        Child, HGroup, Child, g_stageBtn, Child, g_stageAllBtn, End,
        Child, TextObject, MUIA_Text_Contents, "Bereit:", End,
        Child, NListviewObject, MUIA_NListview_NList, g_staged, End,
        Child, g_unstageBtn,
        Child, g_message,
        Child, HGroup, Child, g_all, Child, Label("alle"), Child, HSpace(0), Child, g_commitBtn, End,
    End;
    return g_page;
}

Object *vcspanel_create_history_page(void)
{
    g_historyList = NListObject,
        MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
        MUIA_NList_DestructHook,  MUIV_NList_DestructHook_String,
    End;
    return NListviewObject, MUIA_NListview_NList, g_historyList,
                            MUIA_NListview_Horiz_ScrollBar, MUIV_NListview_HSB_FullAuto, End;
}

static void notify_button(Object *btn, long id)
{
    DoMethod(btn, MUIM_Notify, MUIA_Pressed, FALSE, g_app, 2, MUIM_Application_ReturnID, id);
}

int vcspanel_init(Object *app, Object *window, const VcsHost *host)
{
    g_app = app;
    g_window = window;
    g_host = *host;
    aide_vcs_result_init(&g_status);

    notify_button(g_refreshBtn, ID_VCS_REFRESH);
    notify_button(g_initBtn, ID_VCS_INIT);
    notify_button(g_stageBtn, ID_VCS_STAGE);
    notify_button(g_stageAllBtn, ID_VCS_STAGE_ALL);
    notify_button(g_unstageBtn, ID_VCS_UNSTAGE);
    notify_button(g_commitBtn, ID_VCS_COMMIT);

    g_timerPort = CreateMsgPort();
    if (g_timerPort) {
        g_timerReq = (struct timerequest *)CreateIORequest(g_timerPort, sizeof(struct timerequest));
        if (g_timerReq && OpenDevice((CONST_STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest *)g_timerReq, 0) == 0) {
            g_timerSig = 1UL << g_timerPort->mp_SigBit;
            g_timerOpen = 1;
        }
    }
    update_view();
    vcspanel_tool_changed();
    return g_timerOpen;
}

void vcspanel_cleanup(void)
{
    if (g_timerOpen) {
        if (g_timerArmed) {
            AbortIO((struct IORequest *)g_timerReq);
            WaitIO((struct IORequest *)g_timerReq);
        }
        CloseDevice((struct IORequest *)g_timerReq);
    }
    if (g_timerReq) DeleteIORequest((struct IORequest *)g_timerReq);
    if (g_timerPort) DeleteMsgPort(g_timerPort);
    g_timerOpen = g_timerArmed = 0;
    while (g_queued > 0) pending_free(&g_queue[--g_queued]);
    /* a running amivcs finishes on its own; only its files stay in T: */
    if (g_haveStatus) aide_vcs_result_free(&g_status);
    g_haveStatus = 0;
}

ULONG vcspanel_signal_mask(void)
{
    return g_timerSig;
}

void vcspanel_handle_signals(ULONG got)
{
    if (!(got & g_timerSig) || !g_timerOpen) return;
    if (!CheckIO((struct IORequest *)g_timerReq)) return;
    WaitIO((struct IORequest *)g_timerReq);
    g_timerArmed = 0;
    tick();
}

void vcspanel_tool_changed(void)
{
    g_available = 0;
    g_checked = 0;
    update_view();
    if (!run_simple(AIDE_VCS_VERSION)) { g_checked = 1; update_view(); }
}

void vcspanel_project_changed(const char *projectRoot, const char *aprojPath)
{
    strncpy(g_root, projectRoot ? projectRoot : "", sizeof(g_root) - 1);
    strncpy(g_aproj, aprojPath ? aprojPath : "", sizeof(g_aproj) - 1);
    g_repo = g_root[0] && aide_vcs_present(g_root);
    if (g_haveStatus) { aide_vcs_result_free(&g_status); g_haveStatus = 0; }
    update_view();
    if (g_available && g_repo) {
        check_ignore();
        refresh(1);
    }
}

void vcspanel_file_saved(void)
{
    /* the 68020 hashes the whole tree for a status: only when the page is
     * the one in front would be nicer; a saved file is rare enough */
    if (g_available && g_repo && !g_run.running) run_simple(AIDE_VCS_STATUS);
}

int vcspanel_writing(void)
{
    return g_run.running && aide_vcs_op_writes(g_run.op);
}

/* selected entries of a change list as paths (the text after the kind) */
static void selected_paths(Object *list, AideStrList *out)
{
    LONG pos = MUIV_NList_NextSelected_Start;
    for (;;) {
        char *entry = NULL;
        DoMethod(list, MUIM_NList_NextSelected, &pos);
        if (pos == MUIV_NList_NextSelected_End) break;
        DoMethod(list, MUIM_NList_GetEntry, pos, &entry);
        if (entry && strlen(entry) > 9) aide_strlist_push(out, entry + 9);
    }
}

static int has_dirty_tracked(void)
{
    size_t i;
    if (!g_haveStatus) return 0;
    for (i = 0; i < g_status.changeCount; i++)
        if (g_status.changes[i].staged == 'S' || g_status.changes[i].kind != 'N') return 1;
    return 0;
}

static int prepare_touch(const char *what)
{
    if (g_host.building && g_host.building()) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Ein Build l\344uft. Bitte danach noch einmal.");
        return 0;
    }
    if (MUI_Request(g_app, g_window, 0, "AmigaIDE", "_Weiter|_Abbrechen",
                    "%s kann Dateien des Projekts \344ndern.\n\n"
                    "Datei und Projekt werden vorher gespeichert\n"
                    "und danach neu geladen.", what) != 1)
        return 0;
    if (g_host.save_all && !g_host.save_all()) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Nicht alles konnte gespeichert werden; nichts ge\344ndert.");
        return 0;
    }
    return 1;
}

int vcspanel_handle_id(long id)
{
    AideStrList l;
    char buf[256];

    if (id < ID_VCS_FIRST || id > ID_VCS_LAST) return 0;
    if (id != ID_VCS_CLONE && id != ID_VCS_INIT && (!g_available || !g_repo)) {
        status(g_available ? "Kein Repository -- erst anlegen." : "amivcs ist nicht verf\374gbar.");
        return 1;
    }
    aide_strlist_init(&l);
    switch (id) {
    case ID_VCS_INIT:
        if (!g_available) status("amivcs ist nicht verf\374gbar.");
        else if (!g_root[0]) status("Erst ein Projekt speichern.");
        else if (!g_repo) run_simple(AIDE_VCS_INIT);
        break;
    case ID_VCS_REFRESH:
        refresh(1);
        break;
    case ID_VCS_STAGE:
        selected_paths(g_unstaged, &l);
        if (l.count) run_job(AIDE_VCS_ADD, &l, NULL, 0, NULL);
        break;
    case ID_VCS_STAGE_ALL:
        run_one(AIDE_VCS_ADD, "-A");   /* "." means nothing to AmigaDOS */
        break;
    case ID_VCS_UNSTAGE:
        selected_paths(g_staged, &l);
        if (l.count) run_job(AIDE_VCS_UNSTAGE, &l, NULL, 0, NULL);
        break;
    case ID_VCS_COMMIT: {
        STRPTR msg = NULL;
        ULONG all = FALSE;
        get(g_message, MUIA_String_Contents, &msg);
        get(g_all, MUIA_Selected, &all);
        if (!msg || !msg[0]) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Bitte zuerst eine Commit-Nachricht eingeben.");
            break;
        }
        if (g_haveStatus && aide_vcs_has_theirs(&g_status)
            && MUI_Request(g_app, g_window, 0, "AmigaIDE", "_Trotzdem|_Abbrechen",
                           "Es gibt noch .theirs-Dateien von einem Merge mit\n"
                           "Konflikten. Erst die Konflikte l\366sen und die\n"
                           "Dateien l\366schen.") != 1)
            break;
        if (g_host.save_all) g_host.save_all();   /* commit what the editor shows */
        run_job(AIDE_VCS_COMMIT, NULL, (const char *)msg, all ? 1 : 0, NULL);
        break;
    }
    case ID_VCS_NEWBRANCH:
        buf[0] = '\0';
        if (g_host.ask_string && g_host.ask_string("Neuer Branch", "Name des neuen Branches\n(am aktuellen Commit):",
                                                   buf, sizeof(buf)) && buf[0])
            run_one(AIDE_VCS_BRANCH_NEW, buf);
        break;
    case ID_VCS_SWITCH:
    case ID_VCS_MERGE:
        g_pendingBranchOp = id == ID_VCS_SWITCH ? AIDE_VCS_SWITCH : AIDE_VCS_MERGE;
        if (!prepare_touch(id == ID_VCS_SWITCH ? "Branch wechseln" : "Zusammenf\374hren")) break;
        run_simple(AIDE_VCS_BRANCHES);
        break;
    case ID_VCS_RESTORE:
        if (prepare_touch("Wiederherstellen")) run_simple(AIDE_VCS_CHECKOUT);
        break;
    case ID_VCS_PULL:
        if (has_dirty_tracked()) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Es gibt ungesicherte \304nderungen.\nErst committen, dann Pull.");
            break;
        }
        if (prepare_touch("Pull")) run_simple(AIDE_VCS_PULL);
        break;
    case ID_VCS_PUSH:
        run_simple(AIDE_VCS_PUSH);
        break;
    case ID_VCS_CLONE: {
        char repo[128], parent[200], name[64];
        repo[0] = parent[0] = name[0] = '\0';
        if (!g_available) { status("amivcs ist nicht verf\374gbar."); break; }
        if (!g_host.ask_string || !g_host.ask_string("Clone", "Name des Repositorys auf dem Server\n"
                                                     "(Server und Zugang aus ENV:amivcs.cfg):",
                                                     repo, sizeof(repo)) || !repo[0])
            break;
        if (!g_host.ask_dir || !g_host.ask_dir("Clone: in welches Verzeichnis?", parent, sizeof(parent)) || !parent[0])
            break;
        strncpy(name, repo, sizeof(name) - 1);
        if (!g_host.ask_string("Clone", "Name der neuen Schublade:", name, sizeof(name)) || !name[0])
            break;
        {
            char *target = aide_fs_join(parent, name);
            if (target && aide_fs_exists(target)) {
                MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "%s gibt es schon.", target);
                free(target);
                break;
            }
            strncpy(g_run.cloneTarget, target ? target : "", sizeof(g_run.cloneTarget) - 1);
            free(target);
        }
        aide_strlist_push(&l, repo);
        aide_strlist_push(&l, name);
        if (!run_job(AIDE_VCS_CLONE, &l, NULL, 0, parent)) g_run.cloneTarget[0] = '\0';
        else status("Clone l\344uft...");
        break;
    }
    }
    aide_strlist_free(&l);
    return 1;
}
