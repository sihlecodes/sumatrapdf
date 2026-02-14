/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// TODO: not quite happy how those functions are split among
// Annotation.cpp, EngineMupdf.cpp and EditAnnotations.cpp

// for fast conversions, must match the order of pdf_annot_type enum in annot.h
enum class AnnotationType {
    Text,
    Link,
    FreeText,
    Line,
    Square,
    Circle,
    Polygon,
    PolyLine,
    Highlight,
    Underline,
    Squiggly,
    StrikeOut,
    Redact,
    Stamp,
    Caret,
    Ink,
    Popup,
    FileAttachment,
    Sound,
    Movie,
    RichMedia,
    Widget,
    Screen,
    PrinterMark,
    TrapNet,
    Watermark,
    ThreeD,
    Projection,
    Unknown = -1
};

enum class AnnotationChange {
    Add,
    Remove,
    Modify,
};

class EngineMupdf;
class EngineBase;
extern "C" struct pdf_annot;
struct FileState;

extern const char* gAnnotationTextIcons;

// an user annotation on page
// It abstracts over pdf_annot so that we don't have to
// inlude mupdf to include Annotation
struct Annotation {
    // common to both smx and pdf
    AnnotationType type = AnnotationType::Unknown;
    int pageNo = -1;

    // in page coordinates
    RectF bounds = {};

    EngineMupdf* engine = nullptr;
    pdf_annot* pdfannot = nullptr; // not owned

    // true if this annotation was created by the user (not loaded from PDF)
    // and should be saved to the settings file
    bool isSmx = false;

    // true if a PDF-embedded annotation was modified by the user
    bool isModified = false;

    // original bounds when first loaded from PDF, used to match annotations
    // across sessions for modified/deleted tracking
    RectF originalBounds = {};

    Annotation() = default;
    ~Annotation();
};

struct AnnotCreateArgs {
    AnnotationType annotType = AnnotationType::Unknown;
    // the following are set depending on type of the annotation
    ParsedColor col;
    bool copyToClipboard = false;
    bool setContent = false;
    TempStr content = nullptr;
};

int PageNo(Annotation*);
RectF GetBounds(Annotation*);
RectF GetRect(Annotation*);
void SetRect(Annotation*, RectF);
void SetQuadPointsAsRect(Annotation*, const Vec<RectF>&);
Vec<RectF> GetQuadPointsAsRect(Annotation*);
// Vec<Annotation*> FilterAnnotationsForPage(Vec<Annotation*>* annots, int pageNo);

// EditAnnotations.cpp
const char* Author(Annotation*);
time_t ModificationDate(Annotation*);
int PopupId(Annotation*); // -1 if not exist
TempStr AnnotationReadableNameTemp(AnnotationType tp);
AnnotationType Type(Annotation*);
const char* DefaultAppearanceTextFont(Annotation*);
PdfColor DefaultAppearanceTextColor(Annotation*);
int DefaultAppearanceTextSize(Annotation*);
void SetDefaultAppearanceTextFont(Annotation*, const char*);
void SetDefaultAppearanceTextSize(Annotation*, int);
void SetDefaultAppearanceTextColor(Annotation*, PdfColor);
TempStr Contents(Annotation*);
int Quadding(Annotation*);
bool SetQuadding(Annotation*, int);
int BorderWidth(Annotation*);
void SetBorderWidth(Annotation*, int);
void GetLineEndingStyles(Annotation*, int* start, int* end);
const char* IconName(Annotation*);   // empty() if no icon
PdfColor GetColor(Annotation*);      // kColorUnset if no color
PdfColor InteriorColor(Annotation*); // kColorUnset if no color
bool SetInteriorColor(Annotation*, PdfColor);
int Opacity(Annotation*);
void SetOpacity(Annotation*, int);
bool SetContents(Annotation*, const char*);
bool IsAnnotationEq(Annotation* a1, Annotation* a2);
void PdfColorToFloat(PdfColor c, float rgb[3]);
void SetIconName(Annotation*, const char*);
bool SetColor(Annotation*, PdfColor);
void DeleteAnnotation(Annotation*);
bool IsMoveableAnnotation(AnnotationType);

struct FileState;
void SaveAnnotationsToFileState(EngineBase* engine, FileState* fs);
void RestoreAnnotationsFromFileState(EngineBase* engine, FileState* fs);
void RecordAnnotationDeletion(EngineBase* engine, FileState* fs, Annotation* annot);
AnnotationType AnnotationTypeFromName(const char* name);
TempStr AnnotationTypeNameTemp(AnnotationType tp);
