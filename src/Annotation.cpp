/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/WinUtil.h"

#include "wingui/UIModels.h"

#include "Annotation.h"
#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineMupdf.h"
#include "GlobalPrefs.h"

#include "utils/Log.h"

/*
void SetLineEndingStyles(Annotation*, int start, int end);

Vec<RectF> GetQuadPointsAsRect(Annotation*);
time_t CreationDate(Annotation*);

const char* AnnotationName(AnnotationType);
*/

// spot checks the definitions are the same
static_assert((int)AnnotationType::Link == (int)PDF_ANNOT_LINK);
static_assert((int)AnnotationType::ThreeD == (int)PDF_ANNOT_3D);
static_assert((int)AnnotationType::Sound == (int)PDF_ANNOT_SOUND);
static_assert((int)AnnotationType::Unknown == (int)PDF_ANNOT_UNKNOWN);

// clang-format off
const char* gAnnotationTextIcons = "Comment\0Help\0Insert\0Key\0NewParagraph\0Note\0Paragraph\0";
// clang-format on

// clang format-off

#if 0
// must match the order of enum class AnnotationType
static const char* gAnnotNames =
    "Text\0"
    "Link\0"
    "FreeText\0"
    "Line\0"
    "Square\0"
    "Circle\0"
    "Polygon\0"
    "PolyLine\0"
    "Highlight\0"
    "Underline\0"
    "Squiggly\0"
    "StrikeOut\0"
    "Redact\0"
    "Stamp\0"
    "Caret\0"
    "Ink\0"
    "Popup\0"
    "FileAttachment\0"
    "Sound\0"
    "Movie\0"
    "RichMedia\0"
    "Widget\0"
    "Screen\0"
    "PrinterMark\0"
    "TrapNet\0"
    "Watermark\0"
    "3D\0"
    "Projection\0";
#endif

static const char* gAnnotReadableNames =
    "Text\0"
    "Link\0"
    "Free Text\0"
    "Line\0"
    "Square\0"
    "Circle\0"
    "Polygon\0"
    "Poly Line\0"
    "Highlight\0"
    "Underline\0"
    "Squiggly\0"
    "StrikeOut\0"
    "Redact\0"
    "Stamp\0"
    "Caret\0"
    "Ink\0"
    "Popup\0"
    "File Attachment\0"
    "Sound\0"
    "Movie\0"
    "RichMedia\0"
    "Widget\0"
    "Screen\0"
    "Printer Mark\0"
    "Trap Net\0"
    "Watermark\0"
    "3D\0"
    "Projection\0";
// clang format-on

/*
const char* AnnotationName(AnnotationType tp) {
    int n = (int)tp;
    ReportIf(n < -1 || n > (int)AnnotationType::ThreeD);
    if (n < 0) {
        return "Unknown";
    }
    const char* s = seqstrings::IdxToStr(gAnnotNames, n);
    ReportIf(!s);
    return s;
}
*/

static bool gDebugAnnotDestructor = false;
Annotation::~Annotation() {
    if (gDebugAnnotDestructor) {
        logf("deleting an annotation\n");
    }
}

TempStr AnnotationReadableNameTemp(AnnotationType tp) {
    int n = (int)tp;
    if (n < 0) {
        return (char*)"Unknown";
    }
    char* s = (char*)seqstrings::IdxToStr(gAnnotReadableNames, n);
    ReportIf(!s);
    return s;
}

bool IsAnnotationEq(Annotation* a1, Annotation* a2) {
    if (a1 == a2) {
        return true;
    }
    return a1->pdfannot == a2->pdfannot;
}

AnnotationType Type(Annotation* annot) {
    ReportIf((int)annot->type < 0);
    return annot->type;
}

int PageNo(Annotation* annot) {
    ReportIf(annot->pageNo < 1);
    return annot->pageNo;
}

RectF GetBounds(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    fz_rect rc = {};

    fz_try(ctx) {
        rc = pdf_bound_annot(ctx, annot->pdfannot);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        logf("GetBounds(): pdf_bound_annot() failed\n");
    }
    annot->bounds = ToRectF(rc);
    return annot->bounds;
}

RectF GetRect(Annotation* annot) {
    return annot->bounds;
}

void SetRect(Annotation* annot, RectF r) {
    EngineMupdf* e = annot->engine;
    bool failed = false;
    {
        auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
        fz_rect rc = ToFzRect(r);
        fz_try(ctx) {
            pdf_set_annot_rect(ctx, annot->pdfannot, rc);
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            // can happen for non-moveable annotations
            failed = true;
            logf("SetRect(): pdf_set_annot_rect() or pdf_update_annot() failed\n");
        }
    }
    ReportIf(failed);
    if (failed) {
        return;
    }
    annot->bounds = r;
    MarkNotificationAsModified(e, annot);
}

const char* Author(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);

    const char* s = nullptr;

    fz_var(s);
    fz_try(ctx) {
        s = pdf_annot_author(ctx, annot->pdfannot);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        s = nullptr;
    }
    if (!s || str::IsEmptyOrWhiteSpace(s)) {
        return {};
    }
    return s;
}

int Quadding(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    int res = 0;
    fz_try(ctx) {
        res = pdf_annot_quadding(ctx, annot->pdfannot);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        logf("Quadding(): pdf_annot_quadding() failed\n");
    }
    return res;
}

static bool IsValidQuadding(int i) {
    return i >= 0 && i <= 2;
}

// return true if changed
bool SetQuadding(Annotation* annot, int newQuadding) {
    EngineMupdf* e = annot->engine;
    {
        auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
        ReportIf(!IsValidQuadding(newQuadding));
        bool didChange = Quadding(annot) != newQuadding;
        if (!didChange) {
            return false;
        }
        fz_try(ctx) {
            pdf_set_annot_quadding(ctx, annot->pdfannot, newQuadding);
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            logf("SetQuadding(): pdf_set_annot_quadding or pdf_update_annot() failed\n");
        }
    }
    MarkNotificationAsModified(e, annot);
    return true;
}

void SetQuadPointsAsRect(Annotation* annot, const Vec<RectF>& rects) {
    EngineMupdf* e = annot->engine;
    {
        auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
        fz_quad quads[512];
        int n = rects.Size();
        if (n == 0) {
            return;
        }
        constexpr int kMaxQuads = (int)dimof(quads);
        for (int i = 0; i < n && i < kMaxQuads; i++) {
            RectF rect = rects[i];
            fz_rect r = ToFzRect(rect);
            fz_quad q = fz_quad_from_rect(r);
            quads[i] = q;
        }
        fz_try(ctx) {
            pdf_clear_annot_quad_points(ctx, annot->pdfannot);
            pdf_set_annot_quad_points(ctx, annot->pdfannot, n, quads);
            pdf_update_annot(ctx, annot->pdfannot);
            // update cached bounds after geometry change
            fz_rect rc = pdf_bound_annot(ctx, annot->pdfannot);
            annot->bounds = ToRectF(rc);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            logf("SetQuadPointsAsRect(): mupdf calls failed\n");
        }
    }
    MarkNotificationAsModified(e, annot);
}

Vec<RectF> GetQuadPointsAsRect(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    Vec<RectF> res;
    int n = 0;
    fz_try(ctx) {
        n = pdf_annot_quad_point_count(ctx, annot->pdfannot);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        return res;
    }
    for (int i = 0; i < n; i++) {
        fz_quad q{};
        fz_rect r{};
        fz_try(ctx) {
            q = pdf_annot_quad_point(ctx, annot->pdfannot, i);
            r = fz_rect_from_quad(q);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            continue;
        }
        RectF rect = ToRectF(r);
        res.Append(rect);
    }
    return res;
}

TempStr Contents(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    const char* s = nullptr;
    fz_try(ctx) {
        s = pdf_annot_contents(ctx, annot->pdfannot);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        s = nullptr;
        logf("Contents(): pdf_annot_contents()\n");
    }
    return (TempStr)s;
}

bool SetContents(Annotation* annot, const char* sv) {
    if (!annot) {
        return false;
    }
    EngineMupdf* e = annot->engine;
    const char* currValue = Contents(annot);
    if (str::Eq(sv, currValue)) {
        return false;
    }
    auto ctx = e->Ctx();
    {
        ScopedCritSec cs(e->ctxAccess);
        fz_try(ctx) {
            pdf_set_annot_contents(ctx, annot->pdfannot, sv);
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
    }
    MarkNotificationAsModified(e, annot);
    return true;
}

void DeleteAnnotation(Annotation* annot) {
    if (!annot) {
        return;
    }
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    {
        ScopedCritSec cs(e->ctxAccess);
        bool failed = false;
        pdf_page* page = nullptr;
        fz_try(ctx) {
            page = pdf_annot_page(ctx, annot->pdfannot);
            pdf_delete_annot(ctx, page, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            failed = true;
        }
        if (failed) {
            logf("failed to delete annotation on page %d\n", annot->pageNo);
            return;
        }
    }
    MarkNotificationAsModified(e, annot, AnnotationChange::Remove);
}

// -1 if not exist
int PopupId(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    pdf_obj* obj = nullptr;
    int res = -1;
    fz_try(ctx) {
        obj = pdf_dict_get(ctx, pdf_annot_obj(ctx, annot->pdfannot), PDF_NAME(Popup));
        if (obj) {
            res = pdf_to_num(ctx, obj);
        }
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return res;
}

/*
time_t CreationDate(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    auto pdf = annot->pdf;
    ScopedCritSec cs(e->ctxAccess);
    int64_t res = 0;
    fz_try(ctx)
    {
        res = pdf_annot_creation_date(ctx, annot->pdfannot);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return res;
}
*/

time_t ModificationDate(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    int64_t res = 0;
    fz_try(ctx) {
        res = pdf_annot_modification_date(ctx, annot->pdfannot);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return res;
}

// return empty() if no icon
const char* IconName(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    bool hasIcon = false;
    const char* iconName = nullptr;
    fz_try(ctx) {
        hasIcon = pdf_annot_has_icon_name(ctx, annot->pdfannot);
        if (hasIcon) {
            // can only call if pdf_annot_has_icon_name() returned true
            iconName = pdf_annot_icon_name(ctx, annot->pdfannot);
        }
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return iconName;
}

void SetIconName(Annotation* annot, const char* iconName) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    {
        ScopedCritSec cs(e->ctxAccess);
        fz_try(ctx) {
            pdf_set_annot_icon_name(ctx, annot->pdfannot, iconName);
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
    }
    // TODO: only if the value changed
    MarkNotificationAsModified(e, annot);
}

void PdfColorToFloat(PdfColor c, float rgb[3]) {
    u8 r, g, b, a;
    UnpackPdfColor(c, r, g, b, a);
    rgb[0] = (float)r / 255.0f;
    rgb[1] = (float)g / 255.0f;
    rgb[2] = (float)b / 255.0f;
}

static float GetOpacityFloat(PdfColor c) {
    u8 alpha = GetAlpha(c);
    return alpha / 255.0f;
}

static PdfColor MkPdfColorFromFloat(float rf, float gf, float bf) {
    u8 r = (u8)(rf * 255.0f);
    u8 g = (u8)(gf * 255.0f);
    u8 b = (u8)(bf * 255.0f);
    return MkPdfColor(r, g, b, 0xff);
}

// n = 1 (grey), 3 (rgb) or 4 (cmyk).
static PdfColor PdfColorFromFloat(fz_context* ctx, int n, float color[4]) {
    if (n == 0) {
        return 0; // transparent
    }
    if (n == 1) {
        return MkPdfColorFromFloat(color[0], color[0], color[0]);
    }
    if (n == 3) {
        return MkPdfColorFromFloat(color[0], color[1], color[2]);
    }
    if (n == 4) {
        float rgb[4]{};
        fz_try(ctx) {
            fz_convert_color(ctx, fz_device_cmyk(ctx), color, fz_device_rgb(ctx), rgb, nullptr,
                             fz_default_color_params);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
        return MkPdfColorFromFloat(rgb[0], rgb[1], rgb[2]);
    }
    ReportIf(true);
    return 0;
}

PdfColor GetColor(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    float color[4]{};
    int n = -1;
    fz_try(ctx) {
        pdf_annot_color(ctx, annot->pdfannot, &n, color);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        n = -1;
    }
    if (n == -1) {
        return 0;
    }
    PdfColor res = PdfColorFromFloat(ctx, n, color);
    return res;
}

// return true if color changed
bool SetColor(Annotation* annot, PdfColor c) {
    EngineMupdf* e = annot->engine;
    {
        auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
        bool didChange = false;
        float color[4]{};
        int n = -1;
        float oldOpacity = 0;
        fz_try(ctx) {
            pdf_annot_color(ctx, annot->pdfannot, &n, color);
            oldOpacity = pdf_annot_opacity(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            n = -1;
        }
        if (n == -1) {
            return false;
        }
        float newColor[3];
        PdfColorToFloat(c, newColor);
        float opacity = GetOpacityFloat(c);
        didChange = (n != 3);
        if (!didChange) {
            for (int i = 0; i < n; i++) {
                if (color[i] != newColor[i]) {
                    didChange = true;
                }
            }
        }
        if (opacity != oldOpacity) {
            didChange = true;
        }
        if (!didChange) {
            return false;
        }
        fz_try(ctx) {
            if (c == 0) {
                pdf_set_annot_color(ctx, annot->pdfannot, 0, newColor);
                // TODO: set opacity to 1?
                // pdf_set_annot_opacity(ctx, annot->pdfannot, 1.f);
            } else {
                pdf_set_annot_color(ctx, annot->pdfannot, 3, newColor);
                if (oldOpacity != opacity) {
                    pdf_set_annot_opacity(ctx, annot->pdfannot, opacity);
                }
            }
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
    }
    MarkNotificationAsModified(e, annot);
    return true;
}

PdfColor InteriorColor(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    float color[4]{};
    int n = -1;
    fz_try(ctx) {
        pdf_annot_interior_color(ctx, annot->pdfannot, &n, color);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        n = -1;
    }
    if (n == -1) {
        return 0;
    }
    PdfColor res = PdfColorFromFloat(ctx, n, color);
    return res;
}

bool SetInteriorColor(Annotation* annot, PdfColor c) {
    EngineMupdf* e = annot->engine;
    {
        auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
        bool didChange = false;
        float color[4]{};
        int n = -1;
        fz_try(ctx) {
            pdf_annot_color(ctx, annot->pdfannot, &n, color);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            n = -1;
        }
        if (n == -1) {
            return false;
        }
        float newColor[3]{};
        PdfColorToFloat(c, newColor);
        didChange = (n != 3);
        if (!didChange) {
            for (int i = 0; i < n; i++) {
                if (color[i] != newColor[i]) {
                    didChange = true;
                }
            }
        }
        if (!didChange) {
            return false;
        }
        fz_try(ctx) {
            if (c == 0) {
                pdf_set_annot_interior_color(ctx, annot->pdfannot, 0, newColor);
            } else {
                pdf_set_annot_interior_color(ctx, annot->pdfannot, 3, newColor);
            }
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
    }
    MarkNotificationAsModified(e, annot);
    return true;
}

const char* DefaultAppearanceTextFont(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    const char* fontName = nullptr;
    float sizeF{0.0};
    int n = 0;
    float textColor[4]{};
    fz_try(ctx) {
        pdf_annot_default_appearance(ctx, annot->pdfannot, &fontName, &sizeF, &n, textColor);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return fontName;
}

void SetDefaultAppearanceTextFont(Annotation* annot, const char* sv) {
    EngineMupdf* e = annot->engine;
    {
        auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
        const char* fontName = nullptr;
        float sizeF{0.0};
        int n = 0;
        float textColor[4]{};
        fz_try(ctx) {
            pdf_annot_default_appearance(ctx, annot->pdfannot, &fontName, &sizeF, &n, textColor);
            pdf_set_annot_default_appearance(ctx, annot->pdfannot, sv, sizeF, n, textColor);
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
    }
    MarkNotificationAsModified(e, annot);
}

int DefaultAppearanceTextSize(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    const char* fontName = nullptr;
    float sizeF{0.0};
    int n = 0;
    float textColor[4]{};
    fz_try(ctx) {
        pdf_annot_default_appearance(ctx, annot->pdfannot, &fontName, &sizeF, &n, textColor);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return (int)sizeF;
}

void SetDefaultAppearanceTextSize(Annotation* annot, int textSize) {
    EngineMupdf* e = annot->engine;
    {
        auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
        const char* fontName = nullptr;
        float sizeF{0.0};
        int n = 0;
        float textColor[4]{};
        fz_try(ctx) {
            pdf_annot_default_appearance(ctx, annot->pdfannot, &fontName, &sizeF, &n, textColor);
            pdf_set_annot_default_appearance(ctx, annot->pdfannot, fontName, (float)textSize, n, textColor);
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
    }
    MarkNotificationAsModified(e, annot);
}

PdfColor DefaultAppearanceTextColor(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    const char* fontName = nullptr;
    float sizeF{0.0};
    int n = 0;
    float textColor[4]{};
    fz_try(ctx) {
        pdf_annot_default_appearance(ctx, annot->pdfannot, &fontName, &sizeF, &n, textColor);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    PdfColor res = PdfColorFromFloat(ctx, n, textColor);
    return res;
}

void SetDefaultAppearanceTextColor(Annotation* annot, PdfColor col) {
    EngineMupdf* e = annot->engine;
    {
        auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
        const char* fontName = nullptr;
        float sizeF{0.0};
        int n = 0;
        float textColor[4]{}; // must be at least 4
        fz_try(ctx) {
            pdf_annot_default_appearance(ctx, annot->pdfannot, &fontName, &sizeF, &n, textColor);
            PdfColorToFloat(col, textColor);
            pdf_set_annot_default_appearance(ctx, annot->pdfannot, fontName, sizeF, 3, textColor);
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
    }
    MarkNotificationAsModified(e, annot);
}

void GetLineEndingStyles(Annotation* annot, int* start, int* end) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    pdf_line_ending leStart = PDF_ANNOT_LE_NONE;
    pdf_line_ending leEnd = PDF_ANNOT_LE_NONE;
    fz_try(ctx) {
        pdf_annot_line_ending_styles(ctx, annot->pdfannot, &leStart, &leEnd);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        logf("GetLineEndingStyles: pdf_annot_line_ending_styles() failed\n");
    }
    *start = (int)leStart;
    *end = (int)leEnd;
}

/*
void SetLineEndingStyles(Annotation* annot, int start, int end) {
    EngineMupdf* e = annot->engine;
        {
            auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
                fz_try(ctx)
                {
                        pdf_line_ending leStart = (pdf_line_ending)start;
                        pdf_line_ending leEnd = (pdf_line_ending)end;
                        pdf_set_annot_line_ending_styles(ctx, annot->pdfannot, leStart, leEnd);
                        pdf_update_annot(ctx, annot->pdfannot);
                }
                fz_catch(ctx) {
                        fz_report_error(ctx);
                        logf("SetLineEndingStyles: failure in mupdf calls\n");
                }
        }
    MarkNotificationAsModified(e, annot);
}
*/

int BorderWidth(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    float res = 0;
    fz_try(ctx) {
        res = pdf_annot_border(ctx, annot->pdfannot);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        logf("BorderWidth: pdf_annot_border() failed\n");
    }

    return (int)res;
}

void SetBorderWidth(Annotation* annot, int newWidth) {
    if (!annot) {
        return;
    }
    EngineMupdf* e = annot->engine;
    {
        auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
        fz_try(ctx) {
            pdf_set_annot_border(ctx, annot->pdfannot, (float)newWidth);
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            logf("SetBorderWidth: SetBorderWidth() or pdf_update_annot() failed\n");
        }
    }
    MarkNotificationAsModified(e, annot);
}

int Opacity(Annotation* annot) {
    EngineMupdf* e = annot->engine;
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    float fopacity = 0;
    fz_try(ctx) {
        fopacity = pdf_annot_opacity(ctx, annot->pdfannot);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        logf("Opacity: pdf_annot_opacity() failed\n");
    }
    int res = (int)(fopacity * 255.f);
    return res;
}

void SetOpacity(Annotation* annot, int newOpacity) {
    EngineMupdf* e = annot->engine;
    {
        auto ctx = e->Ctx();
        ScopedCritSec cs(e->ctxAccess);
        ReportIf(newOpacity < 0 || newOpacity > 255);
        newOpacity = std::clamp(newOpacity, 0, 255);
        float fopacity = (float)newOpacity / 255.f;

        fz_try(ctx) {
            pdf_set_annot_opacity(ctx, annot->pdfannot, fopacity);
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            logf("SetOpacity: pdf_set_annot_opacity() or pdf_update_annot() failed\n");
        }
    }
    MarkNotificationAsModified(e, annot);
}

// TODO: unused, remove
#if 0
Vec<Annotation*> FilterAnnotationsForPage(Vec<Annotation*>* annots, int pageNo) {
    Vec<Annotation*> result;
    if (!annots) {
        return result;
    }
    for (auto& annot : *annots) {
        if (annot->isDeleted) {
            continue;
        }
        if (PageNo(annot) != pageNo) {
            continue;
        }
        // include all annotations for pageNo that can be rendered by fz_run_user_annots
        switch (Type(annot)) {
            case AnnotationType::Highlight:
            case AnnotationType::Underline:
            case AnnotationType::StrikeOut:
            case AnnotationType::Squiggly:
                result.Append(annot);
                break;
        }
    }
    return result;
}
#endif

static const char* getuser(void) {
    const char* u;
    u = getenv("USER");
    if (!u) {
        u = getenv("USERNAME");
    }
    if (!u) {
        u = "user";
    }
    return u;
}

static TempStr GetAnnotationTextIconTemp() {
    char* s = str::DupTemp(gGlobalPrefs->annotations.textIconType);
    // this way user can use "new paragraph" and we'll match "NewParagraph"
    str::RemoveCharsInPlace(s, " ");
    int idx = seqstrings::StrToIdxIS(gAnnotationTextIcons, s);
    if (idx < 0) {
        return (char*)"Note";
    }
    char* real = (char*)seqstrings::IdxToStr(gAnnotationTextIcons, idx);
    return real;
}

// clang-format off
static AnnotationType moveableAnnotations[] = {
    AnnotationType::Text,
    AnnotationType::Link,
    AnnotationType::FreeText,
    AnnotationType::Line,
    AnnotationType::Square,
    AnnotationType::Circle,
    AnnotationType::Polygon,
    AnnotationType::PolyLine,
    //AnnotationType::Highlight,
    //AnnotationType::Underline,
    //AnnotationType::Squiggly,
    //AnnotationType::StrikeOut,
    //AnnotationType::Redact,
    AnnotationType::Stamp,
    AnnotationType::Caret,
    //AnnotationType::Ink,
    AnnotationType::Popup,
    AnnotationType::FileAttachment,
    AnnotationType::Sound,
    AnnotationType::Movie,
    //AnnotationType::Widget, // TODO: maybe moveble?
    //AnnotationType::Screen,
    AnnotationType::PrinterMark,
    AnnotationType::TrapNet,
    AnnotationType::Watermark,
    AnnotationType::ThreeD,
    AnnotationType::Unknown,// sentinel value
};
// clang-format on

static bool IsAnnotationInList(AnnotationType tp, AnnotationType* allowed) {
    if (!allowed) {
        return true;
    }
    int i = 0;
    while (allowed[i] != AnnotationType::Unknown) {
        AnnotationType tp2 = allowed[i];
        if (tp2 == tp) {
            return true;
        }
        ++i;
    }
    return false;
}

bool IsMoveableAnnotation(AnnotationType tp) {
    return IsAnnotationInList(tp, moveableAnnotations);
}

Annotation* EngineMupdfCreateAnnotation(EngineBase* engine, int pageNo, PointF pos, AnnotCreateArgs* args) {
    static const float black[3] = {0, 0, 0};

    EngineMupdf* epdf = AsEngineMupdf(engine);
    fz_context* ctx = epdf->Ctx();

    auto pageInfo = epdf->GetFzPageInfo(pageNo, true);
    pdf_annot* annot = nullptr;
    auto typ = args->annotType;
    auto col = args->col;
    {
        ScopedCritSec cs(epdf->ctxAccess);

        fz_try(ctx) {
            auto page = pdf_page_from_fz_page(ctx, pageInfo->page);
            enum pdf_annot_type atyp = (enum pdf_annot_type)typ;

            annot = pdf_create_annot(ctx, page, atyp);

            pdf_set_annot_modification_date(ctx, annot, time(nullptr));
            if (pdf_annot_has_author(ctx, annot)) {
                char* defAuthor = gGlobalPrefs->annotations.defaultAuthor;
                // if "(none)" we don't set it
                if (!str::Eq(defAuthor, "(none)")) {
                    const char* author = getuser();
                    if (!str::IsEmptyOrWhiteSpace(defAuthor)) {
                        author = defAuthor;
                    }
                    pdf_set_annot_author(ctx, annot, author);
                }
            }

            switch (typ) {
                case AnnotationType::Highlight:
                case AnnotationType::Underline:
                case AnnotationType::Squiggly:
                case AnnotationType::StrikeOut: {
                    if (!str::IsEmptyOrWhiteSpace(args->content)) {
                        pdf_set_annot_contents(ctx, annot, args->content);
                    }
                } break;
                case AnnotationType::Text:
                case AnnotationType::FreeText:
                case AnnotationType::Stamp:
                case AnnotationType::Caret:
                case AnnotationType::Square:
                case AnnotationType::Circle: {
                    fz_rect trect = pdf_annot_rect(ctx, annot);
                    float dx = trect.x1 - trect.x0;
                    trect.x0 = pos.x;
                    trect.x1 = trect.x0 + dx;
                    float dy = trect.y1 - trect.y0;
                    trect.y0 = pos.y;
                    trect.y1 = trect.y0 + dy;
                    pdf_set_annot_rect(ctx, annot, trect);
                } break;
                case AnnotationType::Line: {
                    fz_point a{pos.x, pos.y};
                    fz_point b{pos.x + 100, pos.y + 50};
                    pdf_set_annot_line(ctx, annot, a, b);
                } break;
            }
            if (typ == AnnotationType::FreeText) {
                auto& a = gGlobalPrefs->annotations;
                int borderWidth = a.freeTextBorderWidth;
                if (borderWidth < 0) {
                    borderWidth = 1; // default
                }
                pdf_set_annot_border(ctx, annot, (float)borderWidth);
                pdf_set_annot_contents(ctx, annot, "This is a text...");
                int fontSize = a.freeTextSize;
                if (fontSize <= 0) {
                    fontSize = 12;
                }
                int nCol = 3;
                const float* fcol = black;
                float textColor[3]{};

                if (col.parsedOk) {
                    PdfColorToFloat(col.pdfCol, textColor);
                    fcol = textColor;
                }

                pdf_set_annot_default_appearance(ctx, annot, "Helv", (float)fontSize, nCol, fcol);
            }

            pdf_update_annot(ctx, annot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            if (annot) {
                pdf_drop_annot(ctx, annot);
            }
        }
        if (!annot) {
            return nullptr;
        }
    }

    auto res = MakeAnnotationWrapper(epdf, annot, pageNo);
    res->isSmx = true;
    MarkNotificationAsModified(epdf, res, AnnotationChange::Add);

    if (typ == AnnotationType::Text) {
        TempStr iconName = GetAnnotationTextIconTemp();
        if (!str::EqI(iconName, "Note")) {
            SetIconName(res, iconName);
        }
    }
    if (col.parsedOk) {
        SetColor(res, col.pdfCol);
    }

    pdf_drop_annot(ctx, annot);
    return res;
}

// must match the order of enum class AnnotationType
// clang-format off
static const char* gAnnotNames =
    "Text\0"
    "Link\0"
    "FreeText\0"
    "Line\0"
    "Square\0"
    "Circle\0"
    "Polygon\0"
    "PolyLine\0"
    "Highlight\0"
    "Underline\0"
    "Squiggly\0"
    "StrikeOut\0"
    "Redact\0"
    "Stamp\0"
    "Caret\0"
    "Ink\0"
    "Popup\0"
    "FileAttachment\0"
    "Sound\0"
    "Movie\0"
    "RichMedia\0"
    "Widget\0"
    "Screen\0"
    "PrinterMark\0"
    "TrapNet\0"
    "Watermark\0"
    "3D\0"
    "Projection\0";
// clang-format on

AnnotationType AnnotationTypeFromName(const char* name) {
    if (!name || !*name) {
        return AnnotationType::Unknown;
    }
    int idx = seqstrings::StrToIdxIS(gAnnotNames, name);
    if (idx < 0) {
        return AnnotationType::Unknown;
    }
    return (AnnotationType)idx;
}

TempStr AnnotationTypeNameTemp(AnnotationType tp) {
    int n = (int)tp;
    if (n < 0) {
        return (char*)"Unknown";
    }
    const char* s = seqstrings::IdxToStr(gAnnotNames, n);
    if (!s) {
        return (char*)"Unknown";
    }
    return (TempStr)s;
}

// Serialize quad points from a mupdf annotation to a string
// Format: "x,y,dx,dy;x,y,dx,dy;..."
static TempStr SerializeQuadPointsTemp(EngineMupdf* e, Annotation* annot) {
    auto ctx = e->Ctx();
    ScopedCritSec cs(e->ctxAccess);
    int n = 0;
    fz_try(ctx) {
        n = pdf_annot_quad_point_count(ctx, annot->pdfannot);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        return nullptr;
    }
    if (n == 0) {
        return nullptr;
    }
    str::Str s;
    for (int i = 0; i < n; i++) {
        fz_quad q{};
        fz_rect r{};
        fz_try(ctx) {
            q = pdf_annot_quad_point(ctx, annot->pdfannot, i);
            r = fz_rect_from_quad(q);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            continue;
        }
        if (i > 0) {
            s.AppendChar(';');
        }
        s.AppendFmt("%g,%g,%g,%g", r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
    }
    return str::DupTemp(s.Get());
}

// Parse quad points string back into Vec<RectF>
static void ParseQuadPoints(const char* s, Vec<RectF>& rects) {
    if (!s || !*s) {
        return;
    }
    StrVec parts;
    Split(&parts, s, ";");
    for (int i = 0; i < parts.Size(); i++) {
        const char* part = parts.At(i);
        float x = 0, y = 0, dx = 0, dy = 0;
        int nParsed = sscanf(part, "%g,%g,%g,%g", &x, &y, &dx, &dy);
        if (nParsed == 4) {
            rects.Append(RectF(x, y, dx, dy));
        }
    }
}

// Convert PdfColor to a hex color string like "#rrggbbaa"
static TempStr PdfColorToHexTemp(PdfColor c) {
    if (c == 0) {
        return (TempStr) "";
    }
    u8 r, g, b, a;
    UnpackPdfColor(c, r, g, b, a);
    if (a == 0xff) {
        return str::FormatTemp("#%02x%02x%02x", r, g, b);
    }
    return str::FormatTemp("#%02x%02x%02x%02x", a, r, g, b);
}

void SaveAnnotationsToFileState(EngineBase* engine, FileState* fs) {
    if (!engine || !fs) {
        return;
    }
    EngineMupdf* epdf = AsEngineMupdf(engine);
    if (!epdf) {
        return;
    }

    // Clear existing saved annotations
    if (fs->savedAnnotations) {
        for (auto sa : *fs->savedAnnotations) {
            DeleteSavedAnnotation(sa);
        }
        fs->savedAnnotations->Reset();
    }

    // Iterate all pages directly instead of using EngineMupdfGetAnnotations
    // because that function uses GetFzPageInfoFast which skips pages that
    // aren't fully loaded. Restored annotations live on quick-loaded pages
    // and would be lost if we only checked fully-loaded pages.
    Vec<Annotation*> annots;
    {
        ScopedCritSec scope(&epdf->pagesAccess);
        for (int i = 0; i < epdf->pageCount; i++) {
            FzPageInfo* pi = epdf->pages[i];
            if (pi) {
                annots.Append(pi->annotations);
            }
        }
    }

    for (Annotation* annot : annots) {
        if (!annot->isSmx) {
            continue;
        }
        SavedAnnotation* sa = NewSavedAnnotation();
        AnnotationType tp = Type(annot);
        str::ReplaceWithCopy(&sa->type, AnnotationTypeNameTemp(tp));
        sa->pageNo = PageNo(annot);
        logf("SaveAnnotationsToFileState: saving annot type='%s' pageNo=%d rect=(%g,%g,%g,%g)\n",
             sa->type, sa->pageNo, (double)GetBounds(annot).x, (double)GetBounds(annot).y,
             (double)GetBounds(annot).dx, (double)GetBounds(annot).dy);

        PdfColor col = GetColor(annot);
        str::ReplaceWithCopy(&sa->color, PdfColorToHexTemp(col));

        RectF bounds = GetBounds(annot);
        sa->rect = bounds;

        TempStr contents = Contents(annot);
        if (contents && *contents) {
            str::ReplaceWithCopy(&sa->contents, contents);
        }

        PdfColor ic = InteriorColor(annot);
        str::ReplaceWithCopy(&sa->interiorColor, PdfColorToHexTemp(ic));

        sa->borderWidth = BorderWidth(annot);

        const char* iconName = IconName(annot);
        if (iconName && *iconName) {
            str::ReplaceWithCopy(&sa->iconName, iconName);
        }

        sa->opacity = Opacity(annot);
        sa->quadding = Quadding(annot);

        // Serialize quad points for text markup annotations
        if (tp == AnnotationType::Highlight || tp == AnnotationType::Underline ||
            tp == AnnotationType::Squiggly || tp == AnnotationType::StrikeOut) {
            TempStr qp = SerializeQuadPointsTemp(epdf, annot);
            if (qp) {
                str::ReplaceWithCopy(&sa->quadPoints, qp);
            }
        }

        // Save FreeText-specific properties
        if (tp == AnnotationType::FreeText) {
            sa->fontSize = DefaultAppearanceTextSize(annot);
            const char* fontName = DefaultAppearanceTextFont(annot);
            if (fontName && *fontName) {
                str::ReplaceWithCopy(&sa->fontName, fontName);
            }
            PdfColor tc = DefaultAppearanceTextColor(annot);
            str::ReplaceWithCopy(&sa->textColor, PdfColorToHexTemp(tc));
        }

        fs->savedAnnotations->Append(sa);
    }
}

void RestoreAnnotationsFromFileState(EngineBase* engine, FileState* fs) {
    if (!engine || !fs) {
        return;
    }
    EngineMupdf* epdf = AsEngineMupdf(engine);
    if (!epdf || !epdf->pdfdoc) {
        return;
    }
    if (!fs->savedAnnotations || fs->savedAnnotations->Size() == 0) {
        return;
    }

    for (SavedAnnotation* sa : *fs->savedAnnotations) {
        AnnotationType tp = AnnotationTypeFromName(sa->type);
        if (tp == AnnotationType::Unknown) {
            continue;
        }
        int pageNo = sa->pageNo;
        if (pageNo < 1 || pageNo > engine->PageCount()) {
            continue;
        }

        PointF pos{sa->rect.x, sa->rect.y};
        AnnotCreateArgs args;
        args.annotType = tp;

        // Parse color
        if (sa->color && *sa->color) {
            ParseColor(args.col, sa->color);
        }

        Annotation* annot = EngineMupdfCreateAnnotation(engine, pageNo, pos, &args);
        if (!annot) {
            continue;
        }
        annot->isSmx = true;
        // Set the bounding rect
        if (sa->rect.dx > 0 && sa->rect.dy > 0) {
            if (IsMoveableAnnotation(tp)) {
                SetRect(annot, sa->rect);
            }
        }

        // Set contents
        if (sa->contents && *sa->contents) {
            SetContents(annot, sa->contents);
        }

        // Restore quad points for text markup annotations
        if (sa->quadPoints && *sa->quadPoints) {
            Vec<RectF> rects;
            ParseQuadPoints(sa->quadPoints, rects);
            if (rects.Size() > 0) {
                SetQuadPointsAsRect(annot, rects);
            }
        }

        // Set interior color
        if (sa->interiorColor && *sa->interiorColor) {
            ParsedColor ic;
            ParseColor(ic, sa->interiorColor);
            if (ic.parsedOk) {
                SetInteriorColor(annot, ic.pdfCol);
            }
        }

        // Set border width
        if (sa->borderWidth > 0) {
            SetBorderWidth(annot, sa->borderWidth);
        }

        // Set icon name
        if (sa->iconName && *sa->iconName) {
            SetIconName(annot, sa->iconName);
        }

        // Set opacity
        if (sa->opacity < 255) {
            SetOpacity(annot, sa->opacity);
        }

        // Set quadding
        if (sa->quadding > 0) {
            SetQuadding(annot, sa->quadding);
        }

        // Restore FreeText-specific properties
        if (tp == AnnotationType::FreeText) {
            if (sa->fontSize > 0) {
                SetDefaultAppearanceTextSize(annot, sa->fontSize);
            }
            if (sa->fontName && *sa->fontName) {
                SetDefaultAppearanceTextFont(annot, sa->fontName);
            }
            if (sa->textColor && *sa->textColor) {
                ParsedColor tc;
                ParseColor(tc, sa->textColor);
                if (tc.parsedOk) {
                    SetDefaultAppearanceTextColor(annot, tc.pdfCol);
                }
            }
        }
    }

    // Don't clear modifiedAnnotations - these config-only annotations
    // are genuinely unsaved (not in the PDF) and the user should see
    // the "unsaved annotations" indicator
}
