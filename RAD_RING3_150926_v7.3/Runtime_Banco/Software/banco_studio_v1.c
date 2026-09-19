/* ============================================================================
   BANCO STUDIO LBF v1.0 - GUI RAD do BANCO DE DADOS NATIVO (Runtime_Banco)
   Espelho do SQLite Studio (sqlite.c), mas 100% nativo: so chama libbanco.h
   Fluxo: CRIAR DB -> CRIAR TAB -> INSERT -> SELECT / UPDATE / DELETE
   ============================================================================ */
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "Runtime_sdk/components/TOS_IPC.h"
#include "Runtime_Banco/lib_banco/libbanco.h"

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 640;
const int winHeight = 520;

TGUIControl* MemoLog     = NULL;
TGUIControl* EditBanco   = NULL;
TGUIControl* EditTabela  = NULL;
TGUIControl* EditSpec    = NULL;
TGUIControl* EditValores = NULL;
TGUIControl* EditPK      = NULL;
TGUIControl* EditCampo   = NULL;
TGUIControl* EditValor   = NULL;
TGUIControl* BtnCriarDb  = NULL;
TGUIControl* BtnCriarTab = NULL;
TGUIControl* BtnInsert   = NULL;
TGUIControl* BtnSelect   = NULL;
TGUIControl* BtnUpdate   = NULL;
TGUIControl* BtnDelete   = NULL;
TGUIControl* BtnTabelas  = NULL;
TGUIControl* BtnCampos   = NULL;
TGUIControl* BtnCount    = NULL;
TGUIControl* BtnDrop     = NULL;
TGUIControl* BtnLimpar   = NULL;
TGUIControl* BtnSalvarL  = NULL;
TGUIControl* BtnLerL     = NULL;

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
        return key;
    }
    return get_key();
}
void Flush_Grafico_Janela(void) {
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}
void Tratar_Fechamento_Software(void) {
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}
char* GUI_Memo_GetText(TGUIControl* memo) {
    if (!memo || !memo->Buffer) return NULL;
    return (char*)memo->Buffer;
}
static void log_line(const char* m) {
    char buf[300];
    strcpy(buf, m); strcat(buf, "\n");
    if (MemoLog) GUI_Memo_AddStr(MemoLog, buf);
    sys_debug(m);
    Flush_Grafico_Janela();
}
static void log_num(const char* label, int v) {
    char b[96]; char n[16];
    itoa((uint64_t)(uint32_t)v, n, 10);
    strcpy(b, label); strcat(b, n);
    log_line(b);
}
static int dentro(TGUIControl* c, int x, int y) {
    return c && x >= c->Left && x < (c->Left + c->Width) && y >= c->Top && y < (c->Top + c->Height);
}

/* ============================================================================
   EVENTOS DOS BOTOES (chamam SOMENTE a libbanco.h)
   ============================================================================ */
void OnBtnCriarDbClick(void* s) {
    char* banco = GUI_Edit_GetText(EditBanco);
    if (!banco || !banco[0]) { log_line("Nome do banco vazio!"); return; }
    int rc = sys_banco_create_db(banco);
    log_num("create_db rc =", rc);
    if (rc == BCO_OK) log_line("BANCO criado no disco (catalogo .CAT).");
    else if (rc == BCO_ERR_EXISTS) log_line("Banco ja existe - pode usar.");
    else log_line("Erro ao criar banco.");
}
void OnBtnCriarTabClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* spec   = GUI_Edit_GetText(EditSpec);
    if (!banco || !banco[0] || !tabela || !tabela[0] || !spec || !spec[0]) {
        log_line("Preencha banco, tabela e spec!"); return;
    }
    int rc = sys_banco_create_table(banco, tabela, spec);
    log_num("create_table rc =", rc);
    if (rc == BCO_OK) log_line("TABELA criada (.TBL + entrada no catalogo).");
    else if (rc == BCO_ERR_EXISTS) log_line("Tabela ja existe - pode usar.");
    else if (rc == BCO_ERR_PARSE) log_line("Spec invalida! Ex: CODIGO:I4:PK:AI,NOME:C50:NN");
    else log_line("Erro ao criar tabela.");
}
void OnBtnInsertClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* val    = GUI_Edit_GetText(EditValores);
    if (!banco || !banco[0] || !tabela || !tabela[0]) { log_line("Banco/tabela vazios!"); return; }
    int rc = sys_banco_insert(banco, tabela, val ? val : "");
    log_num("insert rc =", rc);
    if (rc == BCO_OK) log_line("INSERT ok (registro gravado no .TBL).");
    else if (rc == BCO_ERR_NOTFOUND) log_line("Tabela nao achada no catalogo.");
    else if (rc == BCO_ERR_FULL) log_line("Tabela cheia (limite do buffer).");
    else log_line("Erro no INSERT.");
}
void OnBtnSelectClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* campo  = GUI_Edit_GetText(EditCampo);
    char* valor  = GUI_Edit_GetText(EditValor);
    static char out[2048];
    int n = sys_banco_select(banco, tabela,
                             (campo && campo[0]) ? campo : 0,
                             (campo && campo[0]) ? valor : 0,
                             out, sizeof(out));
    log_num("select linhas =", n);
    if (n < 0) { log_line("Erro no SELECT."); return; }
    if (n == 0) { log_line("(sem linhas vivas)"); return; }
    log_line(out);
}
void OnBtnUpdateClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* pk     = GUI_Edit_GetText(EditPK);
    char* val    = GUI_Edit_GetText(EditValores);
    int rc = sys_banco_update(banco, tabela, pk, val);
    log_num("update rc =", rc);
    if (rc == 1) log_line("UPDATE aplicado (1 registro).");
    else if (rc == BCO_ERR_NOTFOUND) log_line("PK nao encontrada.");
    else log_line("Erro no UPDATE.");
}
void OnBtnDeleteClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* pk     = GUI_Edit_GetText(EditPK);
    int rc = sys_banco_delete(banco, tabela, pk);
    log_num("delete rc =", rc);
    if (rc == 1) log_line("DELETE ok (registro marcado como removido).");
    else if (rc == BCO_ERR_NOTFOUND) log_line("PK nao encontrada.");
    else log_line("Erro no DELETE.");
}
void OnBtnTabelasClick(void* s) {
    char* banco = GUI_Edit_GetText(EditBanco);
    static char out[1024];
    int rc = sys_banco_list_tables(banco, out, sizeof(out));
    log_num("list_tables rc =", rc);
    if (rc == BCO_OK) log_line(out[0] ? out : "(sem tabelas neste banco)");
    else log_line("Banco nao encontrado.");
}
void OnBtnCamposClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    static char out[1024];
    int rc = sys_banco_list_fields(banco, tabela, out, sizeof(out));
    log_num("list_fields rc =", rc);
    if (rc == BCO_OK) log_line(out[0] ? out : "(tabela sem campos?)");
    else log_line("Tabela nao encontrada.");
}
void OnBtnCountClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    int n = sys_banco_count(banco, tabela);
    log_num("registros vivos =", n);
}
void OnBtnDropClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    int rc = sys_banco_drop_table(banco, tabela);
    log_num("drop rc =", rc);
    if (rc == BCO_OK) log_line("Tabela removida do disco e do catalogo.");
    else log_line("Erro no DROP.");
}
void OnBtnLimparClick(void* s) { GUI_Memo_Clear(MemoLog); Flush_Grafico_Janela(); }
void OnBtnSalvarLogClick(void* s) {
    char* txt = GUI_Memo_GetText(MemoLog);
    if (!txt) txt = "";
    int st = sys_fat_write("0:/banco_log.txt", (void*)txt, (uint32_t)strlen(txt));
    log_num("salvar log =", st);
}
void OnBtnLerLogClick(void* s) {
    char buffer[4097]; memset(buffer, 0, sizeof(buffer));
    int n = sys_fat_read("0:/banco_log.txt", (void*)buffer, 4096);
    GUI_Memo_Clear(MemoLog);
    if (n > 0) GUI_Memo_AddStr(MemoLog, buffer);
    else GUI_Memo_AddStr(MemoLog, "(banco_log.txt vazio/inexistente)\n");
    Flush_Grafico_Janela();
}

/* ============================================================================
   MAIN
   ============================================================================ */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("Banco Studio LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Banco Studio LBF v1.0", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    /* Linha 1: banco + tabela + criacoes */
    GUI_CreateLabel(&MyApp, 10, 42, "Banco:");
    EditBanco  = GUI_CreateEdit(&MyApp, 60, 38, 120, 25, "MERCADO", NULL);
    GUI_CreateLabel(&MyApp, 190, 42, "Tabela:");
    EditTabela = GUI_CreateEdit(&MyApp, 240, 38, 160, 25, "CADASTRO_CLIENTE", NULL);
    BtnCriarDb  = GUI_CreateButton(&MyApp, 410, 38, 85, 25, "CRIAR DB",  OnBtnCriarDbClick);
    BtnCriarTab = GUI_CreateButton(&MyApp, 500, 38, 90, 25, "CRIAR TAB", OnBtnCriarTabClick);

    /* Linha 2: spec da tabela */
    GUI_CreateLabel(&MyApp, 10, 72, "Spec:");
    EditSpec = GUI_CreateEdit(&MyApp, 60, 68, 530, 25, "CODIGO:I4:PK:AI,NOME:C50:NN,NUMERO:I4:NU", NULL);

    /* Linha 3: valores + PK */
    GUI_CreateLabel(&MyApp, 10, 102, "Valores:");
    EditValores = GUI_CreateEdit(&MyApp, 60, 98, 330, 25, "0|JOSE DA SILVA|123", NULL);
    GUI_CreateLabel(&MyApp, 400, 102, "PK:");
    EditPK = GUI_CreateEdit(&MyApp, 430, 98, 60, 25, "1", NULL);

    /* Linha 4: filtro do SELECT */
    GUI_CreateLabel(&MyApp, 10, 132, "Filtro:");
    EditCampo = GUI_CreateEdit(&MyApp, 60, 128, 140, 25, "CODIGO", NULL);
    EditValor = GUI_CreateEdit(&MyApp, 210, 128, 140, 25, "1", NULL);
    GUI_CreateLabel(&MyApp, 360, 132, "(campo vazio = todas)");

    /* Linha 5: DML */
    BtnInsert  = GUI_CreateButton(&MyApp, 10,  158, 80, 25, "INSERT",  OnBtnInsertClick);
    BtnSelect  = GUI_CreateButton(&MyApp, 95,  158, 80, 25, "SELECT",  OnBtnSelectClick);
    BtnUpdate  = GUI_CreateButton(&MyApp, 180, 158, 80, 25, "UPDATE",  OnBtnUpdateClick);
    BtnDelete  = GUI_CreateButton(&MyApp, 265, 158, 80, 25, "DELETE",  OnBtnDeleteClick);
    BtnTabelas = GUI_CreateButton(&MyApp, 350, 158, 80, 25, "TABELAS", OnBtnTabelasClick);
    BtnCampos  = GUI_CreateButton(&MyApp, 435, 158, 80, 25, "CAMPOS",  OnBtnCamposClick);
    BtnCount   = GUI_CreateButton(&MyApp, 520, 158, 70, 25, "COUNT",   OnBtnCountClick);

    /* Linha 6: administracao */
    BtnDrop    = GUI_CreateButton(&MyApp, 10,  188, 80, 25, "DROP TAB", OnBtnDropClick);
    BtnLimpar  = GUI_CreateButton(&MyApp, 95,  188, 80, 25, "LIMPAR",   OnBtnLimparClick);
    BtnSalvarL = GUI_CreateButton(&MyApp, 180, 188, 90, 25, "SALVAR LOG", OnBtnSalvarLogClick);
    BtnLerL    = GUI_CreateButton(&MyApp, 275, 188, 80, 25, "LER LOG",  OnBtnLerLogClick);

    MemoLog = GUI_CreateMemo(&MyApp, 10, 218, 620, 290);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);
    GUI_Memo_AddStr(MemoLog, "Banco Studio LBF pronto (banco NATIVO, sem SQLite).\n");
    GUI_Memo_AddStr(MemoLog, "Fluxo: CRIAR DB -> CRIAR TAB -> INSERT -> SELECT\n");
    GUI_Memo_AddStr(MemoLog, "Spec: CAMPO:TIPO[:PK|AI|NU],... (I1/I2/I4/C<n>)\n");
    GUI_Memo_AddStr(MemoLog, "Valores: 0|JOSE|123  (0 no PK = auto-incremento)\n\n");

    g_focused_control = (void*)EditBanco;
    ultimo_controle_focado = (void*)EditBanco;
    gui_set_prop(EditBanco, PROP_SET_FOCUS, 1);
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) { Tratar_Fechamento_Software(); break; }
        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
        bool precisa_redesenhar = false;
        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }
        if (euTenhoFoco != ultimo_estado_foco) { ultimo_estado_foco = euTenhoFoco; precisa_redesenhar = true; }
        if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
        else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }
        if (euTenhoFoco) {
            char key = Obter_Tecla_Entrada();
            if (key != 0) { GUI_ProcessKeyboard(&MyApp, key); precisa_redesenhar = true; }
            if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
                if (mouse_hold_timer == 0) {
                    int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                    int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                    ultimo_x = rel_x; ultimo_y = rel_y; mouse_hold_timer = 2;
                    if      (dentro(BtnCriarDb,  rel_x, rel_y)) gui_set_prop(BtnCriarDb,  PROP_STATE, 2);
                    else if (dentro(BtnCriarTab, rel_x, rel_y)) gui_set_prop(BtnCriarTab, PROP_STATE, 2);
                    else if (dentro(BtnInsert,   rel_x, rel_y)) gui_set_prop(BtnInsert,   PROP_STATE, 2);
                    else if (dentro(BtnSelect,   rel_x, rel_y)) gui_set_prop(BtnSelect,   PROP_STATE, 2);
                    else if (dentro(BtnUpdate,   rel_x, rel_y)) gui_set_prop(BtnUpdate,   PROP_STATE, 2);
                    else if (dentro(BtnDelete,   rel_x, rel_y)) gui_set_prop(BtnDelete,   PROP_STATE, 2);
                    else if (dentro(BtnTabelas,  rel_x, rel_y)) gui_set_prop(BtnTabelas,  PROP_STATE, 2);
                    else if (dentro(BtnCampos,   rel_x, rel_y)) gui_set_prop(BtnCampos,   PROP_STATE, 2);
                    else if (dentro(BtnCount,    rel_x, rel_y)) gui_set_prop(BtnCount,    PROP_STATE, 2);
                    else if (dentro(BtnDrop,     rel_x, rel_y)) gui_set_prop(BtnDrop,     PROP_STATE, 2);
                    else if (dentro(BtnLimpar,   rel_x, rel_y)) gui_set_prop(BtnLimpar,   PROP_STATE, 2);
                    else if (dentro(BtnSalvarL,  rel_x, rel_y)) gui_set_prop(BtnSalvarL,  PROP_STATE, 2);
                    else if (dentro(BtnLerL,     rel_x, rel_y)) gui_set_prop(BtnLerL,     PROP_STATE, 2);
                    events_process_mouse(rel_x, rel_y, 1, 0);
                    if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                        precisa_redesenhar = true;
                        if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
                    }
                }
                IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
            }
            if (mouse_hold_timer > 0) {
                mouse_hold_timer--;
                if (mouse_hold_timer == 0) {
                    if (BtnCriarDb)  gui_set_prop(BtnCriarDb,  PROP_STATE, 0);
                    if (BtnCriarTab) gui_set_prop(BtnCriarTab, PROP_STATE, 0);
                    if (BtnInsert)   gui_set_prop(BtnInsert,   PROP_STATE, 0);
                    if (BtnSelect)   gui_set_prop(BtnSelect,   PROP_STATE, 0);
                    if (BtnUpdate)   gui_set_prop(BtnUpdate,   PROP_STATE, 0);
                    if (BtnDelete)   gui_set_prop(BtnDelete,   PROP_STATE, 0);
                    if (BtnTabelas)  gui_set_prop(BtnTabelas,  PROP_STATE, 0);
                    if (BtnCampos)   gui_set_prop(BtnCampos,   PROP_STATE, 0);
                    if (BtnCount)    gui_set_prop(BtnCount,    PROP_STATE, 0);
                    if (BtnDrop)     gui_set_prop(BtnDrop,     PROP_STATE, 0);
                    if (BtnLimpar)   gui_set_prop(BtnLimpar,   PROP_STATE, 0);
                    if (BtnSalvarL)  gui_set_prop(BtnSalvarL,  PROP_STATE, 0);
                    if (BtnLerL)     gui_set_prop(BtnLerL,     PROP_STATE, 0);
                    events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                    precisa_redesenhar = true;
                }
            }
        }
        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFoco ? 16 : 32);
    }
    sys_exit();
    return 0;
}
