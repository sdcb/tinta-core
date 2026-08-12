#ifndef TINTA_EDITOR_DOCUMENT_H
#define TINTA_EDITOR_DOCUMENT_H

#include "tinta_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TINTA_EDITOR_ROPE_CHUNK 65536
#define TINTA_EDITOR_LINE_PAGE 128
#define TINTA_EDITOR_LINE_FANOUT 32

typedef struct TintaEditorRopeNode TintaEditorRopeNode;
typedef struct TintaEditorLineNode TintaEditorLineNode;

typedef struct TintaEditorLineMetric {
    size_t length;
    float width;
    float height;
    uint64_t generation;
} TintaEditorLineMetric;

typedef struct TintaEditorLineIndex {
    TintaEditorLineNode *root;
    TintaEditorLineNode *first;
    TintaEditorLineNode *last;
    size_t line_count;
    size_t char_count;
    float total_height;
    float maximum_width;
} TintaEditorLineIndex;

typedef struct TintaEditorUndoRecord {
    size_t position;
    TintaEditorRopeNode *removed;
    TintaEditorRopeNode *inserted;
    size_t anchor_before;
    size_t caret_before;
    size_t anchor_after;
    size_t caret_after;
    size_t cost;
} TintaEditorUndoRecord;

typedef struct TintaEditorDocument {
    TintaEditorRopeNode *root;
    TintaEditorLineIndex lines;
    TintaVec undo;
    TintaVec redo;
    size_t undo_bytes;
    size_t undo_limit;
    size_t text_limit;
    uint64_t revision;
    bool modified;
} TintaEditorDocument;

typedef struct TintaEditorReplaceResult {
    size_t position;
    size_t removed_length;
    size_t inserted_length;
} TintaEditorReplaceResult;

typedef enum TintaEditorUndoCoalesce {
    TINTA_EDITOR_UNDO_COALESCE_TYPING = 1,
    TINTA_EDITOR_UNDO_COALESCE_BACKSPACE = 2,
    TINTA_EDITOR_UNDO_COALESCE_DELETE = 3
} TintaEditorUndoCoalesce;

void tinta_editor_document_init(TintaEditorDocument *document,
                                float default_line_height);
void tinta_editor_document_destroy(TintaEditorDocument *document);

size_t tinta_editor_document_length(const TintaEditorDocument *document);
size_t tinta_editor_document_line_count(const TintaEditorDocument *document);
wchar_t tinta_editor_document_char_at(const TintaEditorDocument *document,
                                      size_t position);
bool tinta_editor_document_copy(const TintaEditorDocument *document,
                                size_t position, size_t length,
                                wchar_t *output);
bool tinta_editor_document_assign(TintaEditorDocument *document,
                                  const wchar_t *text, size_t length,
                                  bool reject_nul, float default_line_height);
bool tinta_editor_document_replace(TintaEditorDocument *document,
                                   size_t start, size_t end,
                                   const wchar_t *text, size_t length,
                                   bool reject_nul,
                                   size_t anchor_before,
                                   size_t caret_before,
                                   size_t anchor_after,
                                   size_t caret_after,
                                   float default_line_height,
                                   bool record_undo,
                                   TintaEditorReplaceResult *result);

size_t tinta_editor_document_line_from_position(
    const TintaEditorDocument *document, size_t position);
size_t tinta_editor_document_line_start(
    const TintaEditorDocument *document, size_t line);
bool tinta_editor_document_get_line(
    const TintaEditorDocument *document, size_t line,
    size_t *start, TintaEditorLineMetric *metric);
bool tinta_editor_document_set_line_metric(
    TintaEditorDocument *document, size_t line, float width, float height,
    uint64_t generation);
size_t tinta_editor_document_line_from_y(
    const TintaEditorDocument *document, float y, float *line_y);
float tinta_editor_document_line_y(
    const TintaEditorDocument *document, size_t line);

bool tinta_editor_document_can_undo(const TintaEditorDocument *document);
bool tinta_editor_document_can_redo(const TintaEditorDocument *document);
bool tinta_editor_document_undo(TintaEditorDocument *document,
                                size_t *anchor, size_t *caret,
                                float default_line_height);
bool tinta_editor_document_redo(TintaEditorDocument *document,
                                size_t *anchor, size_t *caret,
                                float default_line_height);
void tinta_editor_document_clear_history(TintaEditorDocument *document);
void tinta_editor_document_set_undo_limit(TintaEditorDocument *document,
                                          size_t bytes);
void tinta_editor_document_coalesce_last(TintaEditorDocument *document,
                                         TintaEditorUndoCoalesce kind);

#ifdef TINTA_EDITOR_TEST_ALLOCATOR
void tinta_editor_test_allocator_reset(void);
void tinta_editor_test_allocator_fail_on(size_t allocation);
size_t tinta_editor_test_allocator_calls(void);
size_t tinta_editor_test_allocator_live(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
