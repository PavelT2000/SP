/* =====================================================================
   Системное программирование. Лабораторная работа №1
   "Программа с графическим пользовательским интерфейсом"

   Что умеет программа (по ТЗ):
     * рисование мышью: прямая, ломаная, прямоугольник, эллипс,
       многоугольник, текстовая надпись;
     * сохранение рисунка на диск в векторном формате EMF
       (enhanced metafile) - CreateEnhMetaFile / CloseEnhMetaFile;
     * загрузка картинки с диска (GetEnhMetaFile) и продолжение
       рисования поверх неё;
     * Pan  - колесо мыши (вертикально) и Shift+колесо (горизонтально);
       Zoom - Ctrl+колесо (шаг 10%) и Ctrl+Shift+колесо (шаг 50%);
     * печать любой прямоугольной части векторного изображения.

   Сборка:  gcc main.c -o Laba1.exe -mwindows -municode -lgdi32 -lcomdlg32
   ===================================================================== */

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <stdlib.h>

/* ------------------------- модель документа ------------------------- */

#define MAX_SHAPES 1024   /* сколько фигур помещается в документ      */
#define MAX_PTS      64   /* сколько вершин у ломаной/многоугольника  */
#define MAX_TEXT    128   /* длина текстовой надписи                  */
#define TEXT_H       20   /* высота шрифта надписи в мировых единицах */
#define ZOOM_BASE  1000   /* знаменатель дроби масштаба (см. ApplyView) */

typedef enum {
    SH_LINE, SH_POLYLINE, SH_RECT, SH_ELLIPSE, SH_POLYGON, SH_TEXT
} ShapeType;

/* Одна фигура. Все координаты - МИРОВЫЕ (не экранные): они не
   зависят от текущего масштаба и прокрутки, поэтому их можно как есть
   писать в метафайл и на принтер. */
typedef struct {
    ShapeType type;
    COLORREF  color;
    int       nPts;
    POINT     pts[MAX_PTS];
    WCHAR     text[MAX_TEXT];
} Shape;

/* Инструменты рисования (совпадают с пунктами меню "Инструмент"). */
enum { TOOL_LINE, TOOL_POLYLINE, TOOL_RECT, TOOL_ELLIPSE,
       TOOL_POLYGON, TOOL_TEXT, TOOL_SELECT };

/* Всё состояние программы в одной структуре - так проще читать код. */
static struct {
    Shape  shapes[MAX_SHAPES];
    int    nShapes;

    HENHMETAFILE bg;        /* загруженная с диска картинка (фон)     */
    RECT         bgRect;    /* её габариты в мировых координатах      */

    double zoom;            /* масштаб: 1.0 = 100%                    */
    POINT  org;             /* мировая точка в левом верхнем углу окна */

    int      tool;
    COLORREF color;

    /* незавершённая фигура */
    BOOL   dragging;                 /* тянем линию/прямоугольник/эллипс */
    POINT  p0, p1;
    POINT  poly[MAX_PTS];            /* набираемая ломаная/многоугольник */
    int    nPoly;
    POINT  cursor;                   /* курсор в мировых координатах    */
    BOOL   typing;                   /* набираем текстовую надпись       */
    POINT  textPos;
    WCHAR  textBuf[MAX_TEXT];
    int    textLen;

    /* прямоугольник, выделенный для печати */
    BOOL   hasSel;
    RECT   sel;

    WCHAR  file[MAX_PATH];
} g;

static HWND  g_hwnd;
static const WCHAR *TOOL_NAMES[] = {
    L"Прямая", L"Ломаная", L"Прямоугольник", L"Эллипс",
    L"Многоугольник", L"Текст", L"Область печати"
};

/* ------------------------------ меню -------------------------------- */

enum {
    IDM_NEW = 100, IDM_OPEN, IDM_SAVE, IDM_PRINT, IDM_EXIT,
    IDM_COLOR, IDM_ZOOM100, IDM_FIT, IDM_UNDO,
    IDM_TOOL0 = 200            /* IDM_TOOL0 + номер инструмента */
};

static HMENU BuildMenu(void)
{
    HMENU bar  = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU tool = CreatePopupMenu();
    HMENU view = CreatePopupMenu();
    int i;

    AppendMenuW(file, MF_STRING, IDM_NEW,   L"&Новый\tCtrl+N");
    AppendMenuW(file, MF_STRING, IDM_OPEN,  L"&Открыть EMF...\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE,  L"&Сохранить как EMF...\tCtrl+S");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_PRINT, L"&Печать области...\tCtrl+P");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_EXIT,  L"В&ыход");

    for (i = 0; i <= TOOL_SELECT; i++) {
        if (i == TOOL_SELECT) AppendMenuW(tool, MF_SEPARATOR, 0, NULL);
        AppendMenuW(tool, MF_STRING, IDM_TOOL0 + i, TOOL_NAMES[i]);
    }

    AppendMenuW(view, MF_STRING, IDM_ZOOM100, L"Масштаб &100%");
    AppendMenuW(view, MF_STRING, IDM_FIT,     L"&Вписать в окно");
    AppendMenuW(view, MF_SEPARATOR, 0, NULL);
    AppendMenuW(view, MF_STRING, IDM_COLOR,   L"&Цвет...");
    AppendMenuW(view, MF_SEPARATOR, 0, NULL);
    AppendMenuW(view, MF_STRING, IDM_UNDO,    L"&Отменить фигуру\tCtrl+Z");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&Файл");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tool, L"&Инструмент");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&Вид");
    return bar;
}

/* Заголовок окна показывает файл, инструмент и масштаб. */
static void UpdateTitle(void)
{
    WCHAR buf[512];
    wsprintfW(buf, L"ЛР1 - векторный редактор  [%s]   инструмент: %s   масштаб: %d%%",
              g.file[0] ? g.file : L"без имени",
              TOOL_NAMES[g.tool], (int)(g.zoom * 100 + 0.5));
    SetWindowTextW(g_hwnd, buf);
    CheckMenuRadioItem(GetMenu(g_hwnd), IDM_TOOL0, IDM_TOOL0 + TOOL_SELECT,
                       IDM_TOOL0 + g.tool, MF_BYCOMMAND);
}

/* --------------------- преобразование координат ---------------------
   Вместо ручного пересчёта "мир -> экран" настраиваем режим отображения
   GDI: MM_ANISOTROPIC + оконный/видовой экстенты. После этого ВСЕ
   функции рисования (LineTo, Rectangle, PlayEnhMetaFile, TextOut...)
   принимают мировые координаты, а масштабирование и сдвиг GDI делает
   сам:   dev = (log - windowOrg) * viewportExt / windowExt.
   -------------------------------------------------------------------- */

static void ApplyView(HDC hdc)
{
    SetMapMode(hdc, MM_ANISOTROPIC);
    SetWindowExtEx  (hdc, ZOOM_BASE, ZOOM_BASE, NULL);
    SetViewportExtEx(hdc, (int)(ZOOM_BASE * g.zoom),
                          (int)(ZOOM_BASE * g.zoom), NULL);
    SetWindowOrgEx  (hdc, g.org.x, g.org.y, NULL);
    SetViewportOrgEx(hdc, 0, 0, NULL);
}

/* Экранная точка -> мировая (обратное преобразование делает сам GDI). */
static POINT ToWorld(int x, int y)
{
    POINT p = { x, y };
    HDC hdc = GetDC(g_hwnd);
    ApplyView(hdc);
    DPtoLP(hdc, &p, 1);
    ReleaseDC(g_hwnd, hdc);
    return p;
}

/* ---------------------------- рисование ----------------------------- */

static void NormRect(RECT *r, POINT a, POINT b)
{
    r->left   = min(a.x, b.x);  r->top    = min(a.y, b.y);
    r->right  = max(a.x, b.x);  r->bottom = max(a.y, b.y);
}

/* Рисует одну фигуру в произвольный DC: экранный, метафайла или принтера.
   Именно потому, что функция одна, картинка на экране, в EMF и на бумаге
   получается одинаковой. */
static void DrawShape(HDC hdc, const Shape *s)
{
    HPEN   pen = CreatePen(PS_SOLID, 0, s->color);   /* 0 = тонкое перо */
    HPEN   oldPen   = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RECT   r;

    switch (s->type) {
    case SH_LINE:
        MoveToEx(hdc, s->pts[0].x, s->pts[0].y, NULL);
        LineTo  (hdc, s->pts[1].x, s->pts[1].y);
        break;
    case SH_POLYLINE:
        if (s->nPts >= 2) Polyline(hdc, s->pts, s->nPts);
        break;
    case SH_POLYGON:
        if (s->nPts >= 3) Polygon(hdc, s->pts, s->nPts);
        break;
    case SH_RECT:
        NormRect(&r, s->pts[0], s->pts[1]);
        Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        break;
    case SH_ELLIPSE:
        NormRect(&r, s->pts[0], s->pts[1]);
        Ellipse(hdc, r.left, r.top, r.right, r.bottom);
        break;
    case SH_TEXT: {
        /* Высота шрифта задана в мировых единицах, поэтому надпись
           масштабируется вместе с рисунком. */
        HFONT f = CreateFontW(-TEXT_H, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        HFONT oldF = (HFONT)SelectObject(hdc, f);
        int      oldBk = SetBkMode(hdc, TRANSPARENT);
        COLORREF oldC  = SetTextColor(hdc, s->color);
        TextOutW(hdc, s->pts[0].x, s->pts[0].y, s->text, lstrlenW(s->text));
        SetTextColor(hdc, oldC);
        SetBkMode(hdc, oldBk);
        SelectObject(hdc, oldF);
        DeleteObject(f);
        break; }
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

/* Весь документ: сначала загруженный метафайл-фон, потом свои фигуры. */
static void DrawDocument(HDC hdc)
{
    int i;
    if (g.bg) PlayEnhMetaFile(hdc, g.bg, &g.bgRect);
    for (i = 0; i < g.nShapes; i++) DrawShape(hdc, &g.shapes[i]);
}

/* Габариты всего рисунка в мировых координатах (нужны для сохранения,
   печати "целиком" и команды "Вписать в окно"). */
static BOOL DocumentBounds(RECT *out)
{
    BOOL any = FALSE;
    int i, j;
    RECT b = { 0, 0, 0, 0 };

    if (g.bg) { b = g.bgRect; any = TRUE; }

    for (i = 0; i < g.nShapes; i++) {
        const Shape *s = &g.shapes[i];
        for (j = 0; j < s->nPts; j++) {
            POINT p = s->pts[j];
            /* у надписи учитываем ещё и её примерный размер */
            int x2 = p.x, y2 = p.y;
            if (s->type == SH_TEXT) {
                x2 = p.x + lstrlenW(s->text) * TEXT_H * 2 / 3;
                y2 = p.y + TEXT_H;
            }
            if (!any) { b.left = p.x; b.top = p.y; b.right = x2; b.bottom = y2; any = TRUE; }
            else {
                b.left   = min(b.left,   min(p.x, x2));
                b.top    = min(b.top,    min(p.y, y2));
                b.right  = max(b.right,  max(p.x, x2));
                b.bottom = max(b.bottom, max(p.y, y2));
            }
        }
    }
    if (!any) return FALSE;
    InflateRect(&b, 5, 5);          /* небольшой запас по краям */
    *out = b;
    return TRUE;
}

/* Добавить готовую фигуру в документ. */
static Shape *AddShape(ShapeType t)
{
    Shape *s;
    if (g.nShapes >= MAX_SHAPES) return NULL;
    s = &g.shapes[g.nShapes++];
    ZeroMemory(s, sizeof(*s));
    s->type  = t;
    s->color = g.color;
    return s;
}

static void AddTwoPointShape(ShapeType t, POINT a, POINT b)
{
    Shape *s = AddShape(t);
    if (!s) return;
    s->nPts = 2;
    s->pts[0] = a;
    s->pts[1] = b;
}

/* Незавершённая фигура ("резиновая нить") - рисуется только на экране. */
static void DrawPreview(HDC hdc)
{
    Shape tmp;
    ZeroMemory(&tmp, sizeof(tmp));
    tmp.color = g.color;

    if (g.dragging && g.tool != TOOL_SELECT) {
        tmp.type = (g.tool == TOOL_RECT)    ? SH_RECT :
                   (g.tool == TOOL_ELLIPSE) ? SH_ELLIPSE : SH_LINE;
        tmp.nPts = 2; tmp.pts[0] = g.p0; tmp.pts[1] = g.p1;
        DrawShape(hdc, &tmp);
    }
    if (g.nPoly > 0) {                      /* набираемая ломаная/полигон */
        int i;
        for (i = 0; i < g.nPoly; i++) tmp.pts[i] = g.poly[i];
        tmp.pts[g.nPoly] = g.cursor;        /* "хвост" до курсора */
        tmp.nPts = g.nPoly + 1;
        tmp.type = SH_POLYLINE;
        DrawShape(hdc, &tmp);
    }
    if (g.typing && g.textLen > 0) {
        tmp.type = SH_TEXT; tmp.nPts = 1; tmp.pts[0] = g.textPos;
        lstrcpynW(tmp.text, g.textBuf, MAX_TEXT);
        DrawShape(hdc, &tmp);
    }
}

/* Пунктирная рамка выделенной для печати области (на бумагу не идёт). */
static void DrawSelection(HDC hdc)
{
    RECT r;
    HPEN pen, oldPen;
    HBRUSH oldBrush;
    if (g.dragging && g.tool == TOOL_SELECT) NormRect(&r, g.p0, g.p1);
    else if (g.hasSel) r = g.sel;
    else return;

    pen = CreatePen(PS_DOT, 0, RGB(200, 0, 0));
    oldPen   = (HPEN)SelectObject(hdc, pen);
    oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

/* Перерисовка окна через теневой DC - чтобы не мерцало. */
static void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC     hdc = BeginPaint(hwnd, &ps);
    RECT    rc;
    HDC     mem;
    HBITMAP bmp, oldBmp;

    GetClientRect(hwnd, &rc);
    mem    = CreateCompatibleDC(hdc);
    bmp    = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    oldBmp = (HBITMAP)SelectObject(mem, bmp);

    FillRect(mem, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    ApplyView(mem);                 /* дальше всё в мировых координатах */
    DrawDocument(mem);
    DrawPreview(mem);
    DrawSelection(mem);

    SetMapMode(mem, MM_TEXT);       /* вернуть 1:1 перед копированием */
    SetWindowOrgEx(mem, 0, 0, NULL);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* ------------------------- Pan и Zoom колесом -----------------------
   Wheel              - вертикальная прокрутка
   Shift+Wheel        - горизонтальная прокрутка
   Ctrl+Wheel         - масштаб, шаг 10%
   Ctrl+Shift+Wheel   - масштаб, крупный шаг 50%
   -------------------------------------------------------------------- */
static void OnWheel(HWND hwnd, int keys, int delta, int sx, int sy)
{
    int  notches = delta / WHEEL_DELTA;
    BOOL ctrl  = (keys & MK_CONTROL) != 0;
    BOOL shift = (keys & MK_SHIFT)   != 0;

    if (ctrl) {
        POINT scr = { sx, sy }, before, after;
        double f = shift ? 1.5 : 1.1;
        double k = 1.0;
        int i;
        ScreenToClient(hwnd, &scr);
        before = ToWorld(scr.x, scr.y);

        for (i = 0; i < abs(notches); i++) k *= f;
        g.zoom = (notches > 0) ? g.zoom * k : g.zoom / k;
        if (g.zoom < 0.05) g.zoom = 0.05;
        if (g.zoom > 40.0) g.zoom = 40.0;

        /* сдвигаем начало так, чтобы точка под курсором осталась на месте */
        after = ToWorld(scr.x, scr.y);
        g.org.x -= after.x - before.x;
        g.org.y -= after.y - before.y;
    } else {
        int step = (int)(60 * notches / g.zoom);   /* 60 экранных пикселей */
        if (shift) g.org.x -= step;
        else       g.org.y -= step;
    }
    UpdateTitle();
    InvalidateRect(hwnd, NULL, FALSE);
}

/* ------------------- сохранение / загрузка EMF ----------------------- */

static BOOL AskFileName(BOOL save, WCHAR *path)
{
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"Enhanced Metafile (*.emf)\0*.emf\0Все файлы\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"emf";
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST)
                     : (OFN_FILEMUSTEXIST  | OFN_PATHMUSTEXIST);
    return save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}

/* Запись рисунка в enhanced-метафайл.
   CreateEnhMetaFile отдаёт особый DC: всё, что в него нарисовано,
   записывается в файл не картинкой из пикселей, а списком команд GDI -
   это и есть векторный формат. */
static void SaveEmf(void)
{
    WCHAR path[MAX_PATH] = L"";
    RECT  bounds, frame;
    HDC   ref, emf;
    HENHMETAFILE h;
    int   mmX, mmY, pxX, pxY;

    if (!DocumentBounds(&bounds)) {
        MessageBoxW(g_hwnd, L"Рисунок пуст.", L"Сохранение", MB_ICONINFORMATION);
        return;
    }
    if (!AskFileName(TRUE, path)) return;

    ref = GetDC(g_hwnd);
    /* рамка метафайла задаётся в сотых долях миллиметра */
    mmX = GetDeviceCaps(ref, HORZSIZE); pxX = GetDeviceCaps(ref, HORZRES);
    mmY = GetDeviceCaps(ref, VERTSIZE); pxY = GetDeviceCaps(ref, VERTRES);
    frame.left   = bounds.left   * mmX * 100 / pxX;
    frame.right  = bounds.right  * mmX * 100 / pxX;
    frame.top    = bounds.top    * mmY * 100 / pxY;
    frame.bottom = bounds.bottom * mmY * 100 / pxY;

    emf = CreateEnhMetaFileW(ref, path, &frame,
                             L"Laba1\0Vector drawing\0\0");
    ReleaseDC(g_hwnd, ref);
    if (!emf) { MessageBoxW(g_hwnd, L"Не удалось создать метафайл.",
                            L"Ошибка", MB_ICONERROR); return; }

    /* пишем в мировых координатах: масштаб окна на файл не влияет */
    DrawDocument(emf);

    h = CloseEnhMetaFile(emf);
    DeleteEnhMetaFile(h);           /* файл на диске уже создан */

    lstrcpynW(g.file, path, MAX_PATH);
    UpdateTitle();
}

/* Загрузка метафайла. Разбирать его обратно на фигуры не нужно:
   картинка становится "фоном" документа, а новые фигуры рисуются
   поверх. При следующем сохранении фон и новые фигуры снова
   записываются в один метафайл - получается продолжение рисования. */
static void OpenEmf(void)
{
    WCHAR path[MAX_PATH] = L"";
    HENHMETAFILE h;
    ENHMETAHEADER hdr;

    if (!AskFileName(FALSE, path)) return;
    h = GetEnhMetaFileW(path);
    if (!h) { MessageBoxW(g_hwnd, L"Не удалось открыть метафайл.",
                          L"Ошибка", MB_ICONERROR); return; }

    ZeroMemory(&hdr, sizeof(hdr));
    hdr.nSize = sizeof(hdr);
    GetEnhMetaFileHeader(h, sizeof(hdr), &hdr);

    if (g.bg) DeleteEnhMetaFile(g.bg);
    g.bg      = h;
    /* rclBounds - габариты записанной картинки в пикселях (тип RECTL) */
    g.bgRect.left   = hdr.rclBounds.left;
    g.bgRect.top    = hdr.rclBounds.top;
    g.bgRect.right  = hdr.rclBounds.right;
    g.bgRect.bottom = hdr.rclBounds.bottom;
    g.nShapes = 0;
    g.nPoly   = 0;
    g.hasSel  = FALSE;
    lstrcpynW(g.file, path, MAX_PATH);

    SendMessageW(g_hwnd, WM_COMMAND, IDM_FIT, 0);
}

/* ------------------------------ печать ------------------------------
   Печатается прямоугольная часть рисунка, выделенная инструментом
   "Область печати" (если ничего не выделено - весь рисунок).
   Приём тот же, что и на экране: настраиваем MM_ANISOTROPIC так, чтобы
   выбранный мировой прямоугольник занял лист, и рисуем документ. */
static void PrintSelection(void)
{
    PRINTDLGW pd;
    DOCINFOW  di;
    RECT      area;
    HDC       dc;
    int   pw, ph, w, h;
    double sx, sy, s;

    if (g.hasSel) area = g.sel;
    else if (!DocumentBounds(&area)) {
        MessageBoxW(g_hwnd, L"Нечего печатать.", L"Печать", MB_ICONINFORMATION);
        return;
    }
    w = area.right - area.left;
    h = area.bottom - area.top;
    if (w <= 0 || h <= 0) return;

    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner   = g_hwnd;
    pd.Flags       = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION;
    if (!PrintDlgW(&pd)) return;
    dc = pd.hDC;

    pw = GetDeviceCaps(dc, HORZRES);      /* печатная область листа */
    ph = GetDeviceCaps(dc, VERTRES);
    sx = (double)pw / w;
    sy = (double)ph / h;
    s  = (sx < sy) ? sx : sy;             /* вписываем без искажений */

    ZeroMemory(&di, sizeof(di));
    di.cbSize    = sizeof(di);
    di.lpszDocName = L"ЛР1 - фрагмент рисунка";

    if (StartDocW(dc, &di) > 0 && StartPage(dc) > 0) {
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowExtEx  (dc, ZOOM_BASE, ZOOM_BASE, NULL);
        SetViewportExtEx(dc, (int)(ZOOM_BASE * s), (int)(ZOOM_BASE * s), NULL);
        SetWindowOrgEx  (dc, area.left, area.top, NULL);
        SetViewportOrgEx(dc, 0, 0, NULL);
        /* обрезаем всё, что вне выбранного прямоугольника */
        IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
        DrawDocument(dc);
        EndPage(dc);
        EndDoc(dc);
    }
    DeleteDC(dc);
    if (pd.hDevMode)  GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
}

/* ------------------------- команды меню ------------------------------ */

static void FitToWindow(void)
{
    RECT b, rc;
    double sx, sy;
    if (!DocumentBounds(&b)) { g.zoom = 1.0; g.org.x = g.org.y = 0; return; }
    GetClientRect(g_hwnd, &rc);
    sx = (double)rc.right  / (b.right - b.left);
    sy = (double)rc.bottom / (b.bottom - b.top);
    g.zoom = (sx < sy ? sx : sy);
    if (g.zoom < 0.05) g.zoom = 0.05;
    if (g.zoom > 40.0) g.zoom = 40.0;
    g.org.x = b.left;                  /* левый верхний угол рисунка */
    g.org.y = b.top;
}

static void NewDocument(void)
{
    if (g.bg) { DeleteEnhMetaFile(g.bg); g.bg = NULL; }
    g.nShapes = 0; g.nPoly = 0; g.typing = FALSE; g.textLen = 0;
    g.hasSel = FALSE; g.dragging = FALSE;
    g.zoom = 1.0; g.org.x = g.org.y = 0;
    g.file[0] = 0;
}

static void ChooseDrawColor(void)
{
    static COLORREF custom[16];
    CHOOSECOLORW cc;
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize  = sizeof(cc);
    cc.hwndOwner    = g_hwnd;
    cc.lpCustColors = custom;
    cc.rgbResult    = g.color;
    cc.Flags        = CC_RGBINIT | CC_FULLOPEN;
    if (ChooseColorW(&cc)) g.color = cc.rgbResult;
}

/* Завершение набора ломаной / многоугольника (правая кнопка или Enter). */
static void FinishPoly(void)
{
    if (g.nPoly >= 2) {
        Shape *s = AddShape(g.tool == TOOL_POLYGON ? SH_POLYGON : SH_POLYLINE);
        if (s) {
            int i;
            s->nPts = g.nPoly;
            for (i = 0; i < g.nPoly; i++) s->pts[i] = g.poly[i];
        }
    }
    g.nPoly = 0;
}

/* Завершение ввода текстовой надписи. */
static void FinishText(BOOL keep)
{
    if (keep && g.textLen > 0) {
        Shape *s = AddShape(SH_TEXT);
        if (s) {
            s->nPts = 1;
            s->pts[0] = g.textPos;
            lstrcpynW(s->text, g.textBuf, MAX_TEXT);
        }
    }
    g.typing = FALSE;
    g.textLen = 0;
    g.textBuf[0] = 0;
}

static void SetTool(int t)
{
    FinishPoly();
    FinishText(TRUE);
    g.tool = t;
    UpdateTitle();
}

/* -------------------------- оконная процедура ------------------------ */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_CREATE:
        g_hwnd = hwnd;
        return 0;

    case WM_ERASEBKGND:
        return 1;                       /* фон рисуем сами в OnPaint */

    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_MOUSEWHEEL:
        OnWheel(hwnd, GET_KEYSTATE_WPARAM(wp), GET_WHEEL_DELTA_WPARAM(wp),
                GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONDOWN: {
        POINT w = ToWorld(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        SetFocus(hwnd);
        switch (g.tool) {
        case TOOL_LINE: case TOOL_RECT: case TOOL_ELLIPSE: case TOOL_SELECT:
            g.dragging = TRUE; g.p0 = g.p1 = w; SetCapture(hwnd);
            break;
        case TOOL_POLYLINE: case TOOL_POLYGON:
            if (g.nPoly < MAX_PTS - 1) g.poly[g.nPoly++] = w;
            g.cursor = w;
            break;
        case TOOL_TEXT:
            if (g.typing) FinishText(TRUE);
            g.typing = TRUE; g.textPos = w; g.textLen = 0; g.textBuf[0] = 0;
            break;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0; }

    case WM_MOUSEMOVE:
        g.cursor = ToWorld(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (g.dragging) g.p1 = g.cursor;
        if (g.dragging || g.nPoly > 0) InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (g.dragging) {
            g.dragging = FALSE;
            ReleaseCapture();
            g.p1 = ToWorld(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            switch (g.tool) {
            case TOOL_LINE:    AddTwoPointShape(SH_LINE,    g.p0, g.p1); break;
            case TOOL_RECT:    AddTwoPointShape(SH_RECT,    g.p0, g.p1); break;
            case TOOL_ELLIPSE: AddTwoPointShape(SH_ELLIPSE, g.p0, g.p1); break;
            case TOOL_SELECT:
                NormRect(&g.sel, g.p0, g.p1);
                g.hasSel = (g.sel.right > g.sel.left && g.sel.bottom > g.sel.top);
                break;
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_RBUTTONDOWN:                /* правая кнопка завершает набор */
    case WM_LBUTTONDBLCLK:
        FinishPoly();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_CHAR:                       /* ввод текстовой надписи с клавиатуры */
        if (g.typing) {
            WCHAR c = (WCHAR)wp;
            if (c == VK_RETURN)      FinishText(TRUE);
            else if (c == VK_ESCAPE) FinishText(FALSE);
            else if (c == VK_BACK) { if (g.textLen > 0) g.textBuf[--g.textLen] = 0; }
            else if (c >= 32 && g.textLen < MAX_TEXT - 1) {
                g.textBuf[g.textLen++] = c;
                g.textBuf[g.textLen] = 0;
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_KEYDOWN:                    /* цифры 1..7 - быстрый выбор инструмента */
        if (!g.typing) {
            if (wp >= 0x31 && wp <= 0x37) SetTool((int)wp - 0x31);
            else if (wp == VK_RETURN)     FinishPoly();
            else if (wp == VK_ESCAPE) { g.nPoly = 0; g.hasSel = FALSE; }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= IDM_TOOL0 && id <= IDM_TOOL0 + TOOL_SELECT) {
            SetTool(id - IDM_TOOL0);
        } else switch (id) {
        case IDM_NEW:     NewDocument();       UpdateTitle(); break;
        case IDM_OPEN:    OpenEmf();           UpdateTitle(); break;
        case IDM_SAVE:    SaveEmf();                          break;
        case IDM_PRINT:   PrintSelection();                   break;
        case IDM_COLOR:   ChooseDrawColor();                  break;
        case IDM_ZOOM100: g.zoom = 1.0;        UpdateTitle(); break;
        case IDM_FIT:     FitToWindow();       UpdateTitle(); break;
        case IDM_UNDO:    if (g.nShapes > 0) g.nShapes--;     break;
        case IDM_EXIT:    DestroyWindow(hwnd);                break;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0; }

    case WM_DESTROY:
        if (g.bg) DeleteEnhMetaFile(g.bg);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------ точка входа -------------------------- */

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE prev, PWSTR cmd, int show)
{
    WNDCLASSEXW wc;
    MSG    msg;
    HACCEL accel;
    ACCEL  acc[] = {
        { FVIRTKEY | FCONTROL, 0x4E, IDM_NEW   },   /* Ctrl+N */
        { FVIRTKEY | FCONTROL, 0x4F, IDM_OPEN  },   /* Ctrl+O */
        { FVIRTKEY | FCONTROL, 0x53, IDM_SAVE  },   /* Ctrl+S */
        { FVIRTKEY | FCONTROL, 0x50, IDM_PRINT },   /* Ctrl+P */
        { FVIRTKEY | FCONTROL, 0x5A, IDM_UNDO  },   /* Ctrl+Z */
    };

    (void)prev; (void)cmd;

    ZeroMemory(&g, sizeof(g));
    g.zoom  = 1.0;
    g.color = RGB(0, 0, 0);
    g.tool  = TOOL_LINE;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_CROSS);
    wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"Laba1WndClass";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, L"Laba1WndClass", L"ЛР1",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
                             NULL, BuildMenu(), hInst, NULL);
    if (!g_hwnd) return 0;

    UpdateTitle();
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    accel = CreateAcceleratorTableW(acc, (int)(sizeof(acc) / sizeof(acc[0])));
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!TranslateAcceleratorW(g_hwnd, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyAcceleratorTable(accel);
    return (int)msg.wParam;
}
