#include "editor_document.h"

#include <windows.h>

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef TINTA_EDITOR_TEST_ALLOCATOR
static _Thread_local size_t g_editor_allocation_calls;
static _Thread_local size_t g_editor_allocation_fail;
static _Thread_local size_t g_editor_allocation_live;

static bool editor_allocation_fails(void) {
    g_editor_allocation_calls++;
    return g_editor_allocation_fail &&
           g_editor_allocation_calls == g_editor_allocation_fail;
}

static void *editor_test_malloc(size_t bytes) {
    void *result;
    if (editor_allocation_fails()) return NULL;
    result = malloc(bytes);
    if (result) g_editor_allocation_live++;
    return result;
}

static void *editor_test_calloc(size_t count, size_t bytes) {
    void *result;
    if (editor_allocation_fails()) return NULL;
    result = calloc(count, bytes);
    if (result) g_editor_allocation_live++;
    return result;
}

static void *editor_test_realloc(void *memory, size_t bytes) {
    void *result;
    if (editor_allocation_fails()) return NULL;
    result = realloc(memory, bytes);
    if (result && !memory) g_editor_allocation_live++;
    return result;
}

static void editor_test_free(void *memory) {
    if (memory) {
        if (g_editor_allocation_live) g_editor_allocation_live--;
        free(memory);
    }
}

void tinta_editor_test_allocator_reset(void) {
    g_editor_allocation_calls = 0;
    g_editor_allocation_fail = 0;
}

void tinta_editor_test_allocator_fail_on(size_t allocation) {
    g_editor_allocation_calls = 0;
    g_editor_allocation_fail = allocation;
}

size_t tinta_editor_test_allocator_calls(void) {
    return g_editor_allocation_calls;
}

size_t tinta_editor_test_allocator_live(void) {
    return g_editor_allocation_live;
}

#define malloc(bytes) editor_test_malloc(bytes)
#define calloc(count, bytes) editor_test_calloc((count), (bytes))
#define realloc(memory, bytes) editor_test_realloc((memory), (bytes))
#define free(memory) editor_test_free(memory)
#endif

typedef struct TintaEditorChunk {
    LONG references;
    size_t length;
    wchar_t data[1];
} TintaEditorChunk;

struct TintaEditorRopeNode {
    LONG references;
    unsigned short height;
    bool leaf;
    size_t length;
    size_t line_breaks;
    union {
        struct {
            TintaEditorChunk *chunk;
            size_t start;
        } leaf;
        struct {
            TintaEditorRopeNode *left;
            TintaEditorRopeNode *right;
        } branch;
    } value;
};

struct TintaEditorLineNode {
    bool leaf;
    unsigned short count;
    TintaEditorLineNode *parent;
    size_t line_count;
    size_t char_count;
    float total_height;
    float maximum_width;
    union {
        struct {
            TintaEditorLineMetric lines[TINTA_EDITOR_LINE_PAGE];
            TintaEditorLineNode *previous;
            TintaEditorLineNode *next;
        } leaf;
        struct {
            TintaEditorLineNode *children[TINTA_EDITOR_LINE_FANOUT];
        } branch;
    } value;
};

typedef struct TintaEditorLinePool {
    TintaEditorLineNode **nodes;
    size_t count;
    size_t used;
} TintaEditorLinePool;

typedef struct TintaEditorLineBuilder {
    TintaEditorLineNode **leaves;
    size_t leaf_count;
    size_t leaf_index;
    size_t line_index;
    size_t current_length;
    float default_height;
} TintaEditorLineBuilder;

static unsigned short rope_height(const TintaEditorRopeNode *node) {
    return node ? node->height : 0;
}

static size_t rope_length(const TintaEditorRopeNode *node) {
    return node ? node->length : 0;
}

static size_t rope_breaks(const TintaEditorRopeNode *node) {
    return node ? node->line_breaks : 0;
}

static void chunk_add_ref(TintaEditorChunk *chunk) {
    if (chunk) InterlockedIncrement(&chunk->references);
}

static void chunk_release(TintaEditorChunk *chunk) {
    if (chunk && !InterlockedDecrement(&chunk->references)) free(chunk);
}

static TintaEditorChunk *chunk_create(const wchar_t *text, size_t length) {
    TintaEditorChunk *chunk;
    size_t bytes;
    if (length > (SIZE_MAX - offsetof(TintaEditorChunk, data)) /
                     sizeof(wchar_t))
        return NULL;
    bytes = offsetof(TintaEditorChunk, data) + length * sizeof(wchar_t);
    chunk = (TintaEditorChunk *)malloc(bytes ? bytes : 1);
    if (!chunk) return NULL;
    chunk->references = 1;
    chunk->length = length;
    if (length) memcpy(chunk->data, text, length * sizeof(wchar_t));
    return chunk;
}

static void rope_add_ref(TintaEditorRopeNode *node) {
    if (node) InterlockedIncrement(&node->references);
}

static void rope_release(TintaEditorRopeNode *node) {
    if (!node || InterlockedDecrement(&node->references)) return;
    if (node->leaf) {
        chunk_release(node->value.leaf.chunk);
    } else {
        rope_release(node->value.branch.left);
        rope_release(node->value.branch.right);
    }
    free(node);
}

static TintaEditorRopeNode *rope_leaf(TintaEditorChunk *chunk,
                                      size_t start, size_t length) {
    TintaEditorRopeNode *node;
    size_t i;
    if (!chunk || start > chunk->length || length > chunk->length - start)
        return NULL;
    node = (TintaEditorRopeNode *)calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->references = 1;
    node->height = 1;
    node->leaf = true;
    node->length = length;
    node->value.leaf.chunk = chunk;
    node->value.leaf.start = start;
    chunk_add_ref(chunk);
    for (i = 0; i < length; i++)
        if (chunk->data[start + i] == L'\n') node->line_breaks++;
    return node;
}

static TintaEditorRopeNode *rope_branch(TintaEditorRopeNode *left,
                                        TintaEditorRopeNode *right) {
    TintaEditorRopeNode *node;
    if (!left || !right) return NULL;
    node = (TintaEditorRopeNode *)calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->references = 1;
    node->height = (unsigned short)(1 +
        (rope_height(left) > rope_height(right) ?
         rope_height(left) : rope_height(right)));
    node->length = rope_length(left) + rope_length(right);
    node->line_breaks = rope_breaks(left) + rope_breaks(right);
    node->value.branch.left = left;
    node->value.branch.right = right;
    rope_add_ref(left);
    rope_add_ref(right);
    return node;
}

static TintaEditorRopeNode *rope_balance(TintaEditorRopeNode *left,
                                         TintaEditorRopeNode *right) {
    unsigned short left_height = rope_height(left);
    unsigned short right_height = rope_height(right);
    if (!left) {
        rope_add_ref(right);
        return right;
    }
    if (!right) {
        rope_add_ref(left);
        return left;
    }
    if (left_height > right_height + 1) {
        TintaEditorRopeNode *outer;
        TintaEditorRopeNode *inner;
        TintaEditorRopeNode *middle;
        TintaEditorRopeNode *result;
        if (left->leaf) return NULL;
        outer = left->value.branch.left;
        inner = left->value.branch.right;
        if (rope_height(outer) >= rope_height(inner)) {
            middle = rope_branch(inner, right);
            if (!middle) return NULL;
            result = rope_branch(outer, middle);
            rope_release(middle);
            return result;
        }
        if (!inner || inner->leaf) return NULL;
        {
            TintaEditorRopeNode *new_left = rope_branch(
                outer, inner->value.branch.left);
            TintaEditorRopeNode *new_right = rope_branch(
                inner->value.branch.right, right);
            if (!new_left || !new_right) {
                rope_release(new_left);
                rope_release(new_right);
                return NULL;
            }
            result = rope_branch(new_left, new_right);
            rope_release(new_left);
            rope_release(new_right);
            return result;
        }
    }
    if (right_height > left_height + 1) {
        TintaEditorRopeNode *inner;
        TintaEditorRopeNode *outer;
        TintaEditorRopeNode *middle;
        TintaEditorRopeNode *result;
        if (right->leaf) return NULL;
        inner = right->value.branch.left;
        outer = right->value.branch.right;
        if (rope_height(outer) >= rope_height(inner)) {
            middle = rope_branch(left, inner);
            if (!middle) return NULL;
            result = rope_branch(middle, outer);
            rope_release(middle);
            return result;
        }
        if (!inner || inner->leaf) return NULL;
        {
            TintaEditorRopeNode *new_left = rope_branch(
                left, inner->value.branch.left);
            TintaEditorRopeNode *new_right = rope_branch(
                inner->value.branch.right, outer);
            if (!new_left || !new_right) {
                rope_release(new_left);
                rope_release(new_right);
                return NULL;
            }
            result = rope_branch(new_left, new_right);
            rope_release(new_left);
            rope_release(new_right);
            return result;
        }
    }
    return rope_branch(left, right);
}

static TintaEditorRopeNode *rope_concat(TintaEditorRopeNode *left,
                                        TintaEditorRopeNode *right) {
    if (!left) {
        rope_add_ref(right);
        return right;
    }
    if (!right) {
        rope_add_ref(left);
        return left;
    }
    if (rope_height(left) > rope_height(right) + 1) {
        TintaEditorRopeNode *joined;
        TintaEditorRopeNode *result;
        joined = rope_concat(left->value.branch.right, right);
        if (!joined) return NULL;
        result = rope_balance(left->value.branch.left, joined);
        rope_release(joined);
        return result;
    }
    if (rope_height(right) > rope_height(left) + 1) {
        TintaEditorRopeNode *joined;
        TintaEditorRopeNode *result;
        joined = rope_concat(left, right->value.branch.left);
        if (!joined) return NULL;
        result = rope_balance(joined, right->value.branch.right);
        rope_release(joined);
        return result;
    }
    return rope_branch(left, right);
}

static bool rope_split(TintaEditorRopeNode *root, size_t position,
                       TintaEditorRopeNode **left,
                       TintaEditorRopeNode **right) {
    *left = NULL;
    *right = NULL;
    if (!root) return position == 0;
    if (position == 0) {
        rope_add_ref(root);
        *right = root;
        return true;
    }
    if (position == root->length) {
        rope_add_ref(root);
        *left = root;
        return true;
    }
    if (position > root->length) return false;
    if (root->leaf) {
        *left = rope_leaf(root->value.leaf.chunk,
                          root->value.leaf.start, position);
        *right = rope_leaf(root->value.leaf.chunk,
                           root->value.leaf.start + position,
                           root->length - position);
        if (!*left || !*right) {
            rope_release(*left);
            rope_release(*right);
            *left = NULL;
            *right = NULL;
            return false;
        }
        return true;
    }
    if (position < root->value.branch.left->length) {
        TintaEditorRopeNode *middle = NULL;
        if (!rope_split(root->value.branch.left, position, left, &middle))
            return false;
        *right = rope_concat(middle, root->value.branch.right);
        rope_release(middle);
        if (!*right) {
            rope_release(*left);
            *left = NULL;
            return false;
        }
        return true;
    } else {
        TintaEditorRopeNode *middle = NULL;
        if (!rope_split(root->value.branch.right,
                        position - root->value.branch.left->length,
                        &middle, right))
            return false;
        *left = rope_concat(root->value.branch.left, middle);
        rope_release(middle);
        if (!*left) {
            rope_release(*right);
            *right = NULL;
            return false;
        }
        return true;
    }
}

static TintaEditorRopeNode *rope_build_balanced(
    TintaEditorRopeNode **leaves, size_t begin, size_t end) {
    size_t middle;
    TintaEditorRopeNode *left;
    TintaEditorRopeNode *right;
    TintaEditorRopeNode *root;
    if (begin == end) return NULL;
    if (end - begin == 1) {
        rope_add_ref(leaves[begin]);
        return leaves[begin];
    }
    middle = begin + (end - begin) / 2;
    left = rope_build_balanced(leaves, begin, middle);
    right = rope_build_balanced(leaves, middle, end);
    if (!left || !right) {
        rope_release(left);
        rope_release(right);
        return NULL;
    }
    root = rope_branch(left, right);
    rope_release(left);
    rope_release(right);
    return root;
}

static bool rope_from_text(const wchar_t *text, size_t length,
                           bool reject_nul,
                           TintaEditorRopeNode **result) {
    TintaEditorRopeNode **leaves = NULL;
    size_t leaf_capacity;
    size_t leaf_count = 0;
    wchar_t *buffer = NULL;
    size_t buffered = 0;
    size_t i;
    bool success = false;
    *result = NULL;
    if ((!text && length) || length > SIZE_MAX - TINTA_EDITOR_ROPE_CHUNK)
        return false;
    leaf_capacity = length / TINTA_EDITOR_ROPE_CHUNK + 2;
    if (leaf_capacity > SIZE_MAX / sizeof(*leaves)) return false;
    leaves = (TintaEditorRopeNode **)calloc(leaf_capacity, sizeof(*leaves));
    buffer = (wchar_t *)malloc(TINTA_EDITOR_ROPE_CHUNK * sizeof(wchar_t));
    if (!leaves || !buffer) goto done;
    for (i = 0; i < length; i++) {
        wchar_t character = text[i];
        if (!character && reject_nul) goto done;
        if (character == L'\r') {
            character = L'\n';
            if (i + 1 < length && text[i + 1] == L'\n') i++;
        }
        buffer[buffered++] = character;
        if (buffered == TINTA_EDITOR_ROPE_CHUNK) {
            TintaEditorChunk *chunk = chunk_create(buffer, buffered);
            TintaEditorRopeNode *leaf = chunk ? rope_leaf(chunk, 0, buffered) : NULL;
            chunk_release(chunk);
            if (!leaf) goto done;
            leaves[leaf_count++] = leaf;
            buffered = 0;
        }
    }
    if (buffered) {
        TintaEditorChunk *chunk = chunk_create(buffer, buffered);
        TintaEditorRopeNode *leaf = chunk ? rope_leaf(chunk, 0, buffered) : NULL;
        chunk_release(chunk);
        if (!leaf) goto done;
        leaves[leaf_count++] = leaf;
    }
    *result = rope_build_balanced(leaves, 0, leaf_count);
    if (leaf_count && !*result) goto done;
    success = true;
done:
    for (i = 0; i < leaf_count; i++) rope_release(leaves[i]);
    free(buffer);
    free(leaves);
    if (!success) {
        rope_release(*result);
        *result = NULL;
    }
    return success;
}

static wchar_t rope_char_at(const TintaEditorRopeNode *node, size_t position) {
    while (node && !node->leaf) {
        size_t left_length = node->value.branch.left->length;
        if (position < left_length)
            node = node->value.branch.left;
        else {
            position -= left_length;
            node = node->value.branch.right;
        }
    }
    if (!node || position >= node->length) return 0;
    return node->value.leaf.chunk->data[node->value.leaf.start + position];
}

static void rope_copy_range(const TintaEditorRopeNode *node,
                            size_t position, size_t length,
                            wchar_t *output, size_t *written) {
    size_t left_length;
    size_t take;
    if (!node || !length) return;
    if (node->leaf) {
        take = node->length - position;
        if (take > length) take = length;
        memcpy(output + *written,
               node->value.leaf.chunk->data +
                   node->value.leaf.start + position,
               take * sizeof(wchar_t));
        *written += take;
        return;
    }
    left_length = node->value.branch.left->length;
    if (position < left_length) {
        take = left_length - position;
        if (take > length) take = length;
        rope_copy_range(node->value.branch.left, position, take,
                        output, written);
        length -= take;
        position = 0;
    } else {
        position -= left_length;
    }
    if (length)
        rope_copy_range(node->value.branch.right, position, length,
                        output, written);
}

typedef bool (*RopeVisitFn)(const wchar_t *, size_t, void *);

static bool rope_visit(const TintaEditorRopeNode *node,
                       RopeVisitFn visit, void *context) {
    if (!node) return true;
    if (node->leaf)
        return visit(node->value.leaf.chunk->data + node->value.leaf.start,
                     node->length, context);
    return rope_visit(node->value.branch.left, visit, context) &&
           rope_visit(node->value.branch.right, visit, context);
}

static void line_recalculate(TintaEditorLineNode *node) {
    size_t i;
    node->line_count = 0;
    node->char_count = 0;
    node->total_height = 0;
    node->maximum_width = 0;
    if (node->leaf) {
        node->line_count = node->count;
        for (i = 0; i < node->count; i++) {
            TintaEditorLineMetric *line = &node->value.leaf.lines[i];
            node->char_count += line->length;
            node->total_height += line->height;
            if (line->width > node->maximum_width)
                node->maximum_width = line->width;
        }
    } else {
        for (i = 0; i < node->count; i++) {
            TintaEditorLineNode *child = node->value.branch.children[i];
            node->line_count += child->line_count;
            node->char_count += child->char_count;
            node->total_height += child->total_height;
            if (child->maximum_width > node->maximum_width)
                node->maximum_width = child->maximum_width;
        }
    }
}

static void line_recalculate_up(TintaEditorLineNode *node) {
    while (node) {
        line_recalculate(node);
        node = node->parent;
    }
}

static void line_index_sync(TintaEditorLineIndex *index) {
    if (!index->root) {
        index->line_count = 0;
        index->char_count = 0;
        index->total_height = 0;
        index->maximum_width = 0;
        return;
    }
    index->line_count = index->root->line_count;
    index->char_count = index->root->char_count;
    index->total_height = index->root->total_height;
    index->maximum_width = index->root->maximum_width;
}

static void line_node_destroy(TintaEditorLineNode *node) {
    size_t i;
    if (!node) return;
    if (!node->leaf)
        for (i = 0; i < node->count; i++)
            line_node_destroy(node->value.branch.children[i]);
    free(node);
}

static void line_index_destroy(TintaEditorLineIndex *index) {
    line_node_destroy(index->root);
    memset(index, 0, sizeof(*index));
}

static TintaEditorLineNode *line_node_create(bool leaf) {
    TintaEditorLineNode *node =
        (TintaEditorLineNode *)calloc(1, sizeof(*node));
    if (node) node->leaf = leaf;
    return node;
}

static bool line_builder_feed(const wchar_t *text, size_t length,
                              void *context) {
    TintaEditorLineBuilder *builder = (TintaEditorLineBuilder *)context;
    size_t i;
    for (i = 0; i < length; i++) {
        builder->current_length++;
        if (text[i] == L'\n') {
            TintaEditorLineNode *leaf = builder->leaves[builder->leaf_index];
            TintaEditorLineMetric *line =
                &leaf->value.leaf.lines[builder->line_index++];
            line->length = builder->current_length;
            line->height = builder->default_height;
            builder->current_length = 0;
            if (builder->line_index == TINTA_EDITOR_LINE_PAGE) {
                leaf->count = TINTA_EDITOR_LINE_PAGE;
                line_recalculate(leaf);
                builder->leaf_index++;
                builder->line_index = 0;
            }
        }
    }
    return true;
}

static bool line_index_build(TintaEditorLineIndex *index,
                             const TintaEditorRopeNode *root,
                             float default_height) {
    size_t lines = rope_breaks(root) + 1;
    size_t leaf_count = (lines + TINTA_EDITOR_LINE_PAGE - 1) /
                        TINTA_EDITOR_LINE_PAGE;
    TintaEditorLineNode **level = NULL;
    size_t level_count = leaf_count;
    size_t i;
    TintaEditorLineBuilder builder;
    TintaEditorLineIndex built = {0};
    level = (TintaEditorLineNode **)calloc(level_count, sizeof(*level));
    if (!level) return false;
    for (i = 0; i < level_count; i++) {
        level[i] = line_node_create(true);
        if (!level[i]) goto failed;
        if (i) {
            level[i - 1]->value.leaf.next = level[i];
            level[i]->value.leaf.previous = level[i - 1];
        }
    }
    memset(&builder, 0, sizeof(builder));
    builder.leaves = level;
    builder.leaf_count = leaf_count;
    builder.default_height = default_height;
    if (!rope_visit(root, line_builder_feed, &builder)) goto failed;
    {
        TintaEditorLineNode *leaf = level[builder.leaf_index];
        TintaEditorLineMetric *line =
            &leaf->value.leaf.lines[builder.line_index++];
        line->length = builder.current_length;
        line->height = default_height;
        leaf->count = (unsigned short)builder.line_index;
        line_recalculate(leaf);
    }
    built.first = level[0];
    built.last = level[level_count - 1];
    while (level_count > 1) {
        size_t next_count = (level_count + TINTA_EDITOR_LINE_FANOUT - 1) /
                            TINTA_EDITOR_LINE_FANOUT;
        TintaEditorLineNode **next = (TintaEditorLineNode **)calloc(
            next_count, sizeof(*next));
        if (!next) goto failed_tree;
        for (i = 0; i < next_count; i++) {
            size_t start = i * TINTA_EDITOR_LINE_FANOUT;
            size_t count = level_count - start;
            size_t child;
            if (count > TINTA_EDITOR_LINE_FANOUT)
                count = TINTA_EDITOR_LINE_FANOUT;
            next[i] = line_node_create(false);
            if (!next[i]) {
                size_t j;
                for (j = 0; j < i; j++) free(next[j]);
                free(next);
                goto failed_tree;
            }
            next[i]->count = (unsigned short)count;
            for (child = 0; child < count; child++) {
                next[i]->value.branch.children[child] = level[start + child];
                level[start + child]->parent = next[i];
            }
            line_recalculate(next[i]);
        }
        free(level);
        level = next;
        level_count = next_count;
    }
    built.root = level[0];
    free(level);
    line_index_sync(&built);
    line_index_destroy(index);
    *index = built;
    return true;

failed_tree:
    if (level_count) {
        TintaEditorLineNode *top = level[0];
        while (top && top->parent) top = top->parent;
        line_node_destroy(top);
    }
    free(level);
    return false;
failed:
    for (i = 0; i < level_count; i++) free(level[i]);
    free(level);
    return false;
}

static unsigned line_tree_height(const TintaEditorLineIndex *index) {
    unsigned height = 0;
    TintaEditorLineNode *node = index->root;
    while (node) {
        height++;
        node = node->leaf || !node->count ? NULL :
            node->value.branch.children[0];
    }
    return height;
}

static bool line_pool_init(TintaEditorLinePool *pool, size_t inserted_lines,
                           unsigned height) {
    size_t leaf_splits = inserted_lines / (TINTA_EDITOR_LINE_PAGE / 2) + 4;
    size_t internal_splits = leaf_splits / (TINTA_EDITOR_LINE_FANOUT / 2) +
                             height * 2 + 8;
    size_t count = leaf_splits + internal_splits;
    size_t i;
    memset(pool, 0, sizeof(*pool));
    if (count > SIZE_MAX / sizeof(*pool->nodes)) return false;
    pool->nodes = (TintaEditorLineNode **)calloc(count, sizeof(*pool->nodes));
    if (!pool->nodes) return false;
    pool->count = count;
    for (i = 0; i < count; i++) {
        pool->nodes[i] = (TintaEditorLineNode *)calloc(1, sizeof(TintaEditorLineNode));
        if (!pool->nodes[i]) {
            while (i) free(pool->nodes[--i]);
            free(pool->nodes);
            memset(pool, 0, sizeof(*pool));
            return false;
        }
    }
    return true;
}

static TintaEditorLineNode *line_pool_take(TintaEditorLinePool *pool,
                                           bool leaf) {
    TintaEditorLineNode *node;
    if (!pool || pool->used >= pool->count) return NULL;
    node = pool->nodes[pool->used++];
    memset(node, 0, sizeof(*node));
    node->leaf = leaf;
    return node;
}

static void line_pool_destroy(TintaEditorLinePool *pool) {
    size_t i;
    if (!pool) return;
    for (i = pool->used; i < pool->count; i++) free(pool->nodes[i]);
    free(pool->nodes);
    memset(pool, 0, sizeof(*pool));
}

static TintaEditorLineNode *line_find(TintaEditorLineIndex *index,
                                      size_t line, size_t *offset) {
    TintaEditorLineNode *node = index->root;
    if (!node) return NULL;
    if (line > index->line_count) line = index->line_count;
    while (!node->leaf) {
        size_t i;
        for (i = 0; i < node->count; i++) {
            TintaEditorLineNode *child = node->value.branch.children[i];
            if (line < child->line_count ||
                (line == child->line_count && i + 1 == node->count)) {
                node = child;
                break;
            }
            line -= child->line_count;
        }
    }
    if (offset) *offset = line;
    return node;
}

static size_t line_child_index(TintaEditorLineNode *parent,
                               TintaEditorLineNode *child) {
    size_t i;
    for (i = 0; i < parent->count; i++)
        if (parent->value.branch.children[i] == child) return i;
    return SIZE_MAX;
}

static bool line_insert_sibling(TintaEditorLineIndex *index,
                                TintaEditorLineNode *node,
                                TintaEditorLineNode *sibling,
                                TintaEditorLinePool *pool) {
    TintaEditorLineNode *parent = node->parent;
    size_t position;
    if (!parent) {
        TintaEditorLineNode *root = line_pool_take(pool, false);
        if (!root) return false;
        root->count = 2;
        root->value.branch.children[0] = node;
        root->value.branch.children[1] = sibling;
        node->parent = root;
        sibling->parent = root;
        line_recalculate(root);
        index->root = root;
        return true;
    }
    position = line_child_index(parent, node);
    if (position == SIZE_MAX) return false;
    if (parent->count < TINTA_EDITOR_LINE_FANOUT) {
        memmove(&parent->value.branch.children[position + 2],
                &parent->value.branch.children[position + 1],
                (parent->count - position - 1) *
                    sizeof(parent->value.branch.children[0]));
        parent->value.branch.children[position + 1] = sibling;
        sibling->parent = parent;
        parent->count++;
        line_recalculate_up(parent);
        return true;
    }
    {
        TintaEditorLineNode *temporary[TINTA_EDITOR_LINE_FANOUT + 1];
        TintaEditorLineNode *new_parent = line_pool_take(pool, false);
        size_t i;
        size_t lower = (TINTA_EDITOR_LINE_FANOUT + 1) / 2;
        if (!new_parent) return false;
        for (i = 0; i <= TINTA_EDITOR_LINE_FANOUT; i++) {
            if (i <= position)
                temporary[i] = parent->value.branch.children[i];
            else if (i == position + 1)
                temporary[i] = sibling;
            else
                temporary[i] = parent->value.branch.children[i - 1];
        }
        memset(parent->value.branch.children, 0,
               sizeof(parent->value.branch.children));
        parent->count = (unsigned short)lower;
        for (i = 0; i < lower; i++) {
            parent->value.branch.children[i] = temporary[i];
            temporary[i]->parent = parent;
        }
        new_parent->count = (unsigned short)(TINTA_EDITOR_LINE_FANOUT + 1 - lower);
        for (i = 0; i < new_parent->count; i++) {
            new_parent->value.branch.children[i] = temporary[lower + i];
            temporary[lower + i]->parent = new_parent;
        }
        line_recalculate(parent);
        line_recalculate(new_parent);
        return line_insert_sibling(index, parent, new_parent, pool);
    }
}

static bool line_index_insert_one(TintaEditorLineIndex *index, size_t line,
                                  const TintaEditorLineMetric *metric,
                                  TintaEditorLinePool *pool) {
    size_t offset;
    TintaEditorLineNode *leaf = line_find(index, line, &offset);
    if (!leaf) return false;
    if (leaf->count == TINTA_EDITOR_LINE_PAGE) {
        TintaEditorLineNode *sibling = line_pool_take(pool, true);
        size_t lower = TINTA_EDITOR_LINE_PAGE / 2;
        if (!sibling) return false;
        sibling->count = TINTA_EDITOR_LINE_PAGE - (unsigned short)lower;
        memcpy(sibling->value.leaf.lines,
               &leaf->value.leaf.lines[lower],
               sibling->count * sizeof(TintaEditorLineMetric));
        memset(&leaf->value.leaf.lines[lower], 0,
               sibling->count * sizeof(TintaEditorLineMetric));
        leaf->count = (unsigned short)lower;
        sibling->value.leaf.next = leaf->value.leaf.next;
        sibling->value.leaf.previous = leaf;
        if (leaf->value.leaf.next)
            leaf->value.leaf.next->value.leaf.previous = sibling;
        else
            index->last = sibling;
        leaf->value.leaf.next = sibling;
        line_recalculate(leaf);
        line_recalculate(sibling);
        if (!line_insert_sibling(index, leaf, sibling, pool)) return false;
        if (offset > lower) {
            offset -= lower;
            leaf = sibling;
        }
    }
    if (offset > leaf->count) offset = leaf->count;
    memmove(&leaf->value.leaf.lines[offset + 1],
            &leaf->value.leaf.lines[offset],
            (leaf->count - offset) * sizeof(TintaEditorLineMetric));
    leaf->value.leaf.lines[offset] = *metric;
    leaf->count++;
    line_recalculate_up(leaf);
    line_index_sync(index);
    return true;
}

static void line_remove_empty_node(TintaEditorLineIndex *index,
                                   TintaEditorLineNode *node) {
    TintaEditorLineNode *parent = node->parent;
    if (!parent) return;
    {
        size_t position = line_child_index(parent, node);
        if (position == SIZE_MAX) return;
        memmove(&parent->value.branch.children[position],
                &parent->value.branch.children[position + 1],
                (parent->count - position - 1) *
                    sizeof(parent->value.branch.children[0]));
        parent->count--;
        parent->value.branch.children[parent->count] = NULL;
    }
    free(node);
    if (parent == index->root && parent->count == 1) {
        index->root = parent->value.branch.children[0];
        index->root->parent = NULL;
        free(parent);
    } else if (!parent->count) {
        line_remove_empty_node(index, parent);
    } else {
        line_recalculate_up(parent);
    }
}

static void line_index_delete(TintaEditorLineIndex *index,
                              size_t line, size_t count) {
    while (count) {
        size_t offset;
        TintaEditorLineNode *leaf = line_find(index, line, &offset);
        size_t take;
        if (!leaf || offset >= leaf->count) break;
        take = leaf->count - offset;
        if (take > count) take = count;
        memmove(&leaf->value.leaf.lines[offset],
                &leaf->value.leaf.lines[offset + take],
                (leaf->count - offset - take) * sizeof(TintaEditorLineMetric));
        memset(&leaf->value.leaf.lines[leaf->count - take], 0,
               take * sizeof(TintaEditorLineMetric));
        leaf->count -= (unsigned short)take;
        count -= take;
        if (!leaf->count && leaf != index->root) {
            if (leaf->value.leaf.previous)
                leaf->value.leaf.previous->value.leaf.next = leaf->value.leaf.next;
            else
                index->first = leaf->value.leaf.next;
            if (leaf->value.leaf.next)
                leaf->value.leaf.next->value.leaf.previous =
                    leaf->value.leaf.previous;
            else
                index->last = leaf->value.leaf.previous;
            line_remove_empty_node(index, leaf);
        } else {
            line_recalculate_up(leaf);
        }
    }
    line_index_sync(index);
}

static bool line_index_replace(TintaEditorLineIndex *index,
                               size_t first_line, size_t removed_lines,
                               const TintaEditorLineMetric *inserted,
                               size_t inserted_lines,
                               TintaEditorLinePool *pool) {
    size_t i;
    line_index_delete(index, first_line, removed_lines);
    for (i = 0; i < inserted_lines; i++)
        if (!line_index_insert_one(index, first_line + i,
                                   &inserted[i], pool))
            return false;
    return true;
}

static TintaEditorLineNode *line_find_const(
    const TintaEditorLineIndex *index, size_t line, size_t *offset,
    size_t *characters_before, float *height_before) {
    TintaEditorLineNode *node = index->root;
    size_t characters = 0;
    float height = 0;
    if (!node || line >= index->line_count) return NULL;
    while (!node->leaf) {
        size_t i;
        for (i = 0; i < node->count; i++) {
            TintaEditorLineNode *child = node->value.branch.children[i];
            if (line < child->line_count) {
                node = child;
                break;
            }
            line -= child->line_count;
            characters += child->char_count;
            height += child->total_height;
        }
    }
    if (offset) *offset = line;
    if (characters_before) {
        size_t i;
        for (i = 0; i < line; i++)
            characters += node->value.leaf.lines[i].length;
        *characters_before = characters;
    }
    if (height_before) {
        size_t i;
        for (i = 0; i < line; i++)
            height += node->value.leaf.lines[i].height;
        *height_before = height;
    }
    return node;
}

static size_t line_from_position(const TintaEditorLineIndex *index,
                                 size_t position) {
    TintaEditorLineNode *node = index->root;
    size_t line = 0;
    if (!node || !index->line_count) return 0;
    if (position >= index->char_count) return index->line_count - 1;
    while (!node->leaf) {
        size_t i;
        for (i = 0; i < node->count; i++) {
            TintaEditorLineNode *child = node->value.branch.children[i];
            if (position < child->char_count) {
                node = child;
                break;
            }
            position -= child->char_count;
            line += child->line_count;
        }
    }
    {
        size_t i;
        for (i = 0; i < node->count; i++) {
            size_t length = node->value.leaf.lines[i].length;
            if (position < length) return line + i;
            position -= length;
        }
    }
    return index->line_count - 1;
}

static size_t line_from_height(const TintaEditorLineIndex *index,
                               float y, float *line_y) {
    TintaEditorLineNode *node = index->root;
    size_t line = 0;
    float before = 0;
    if (!node || !index->line_count) {
        if (line_y) *line_y = 0;
        return 0;
    }
    if (y < 0) y = 0;
    while (!node->leaf) {
        size_t i;
        for (i = 0; i < node->count; i++) {
            TintaEditorLineNode *child = node->value.branch.children[i];
            if (y < child->total_height || i + 1 == node->count) {
                node = child;
                break;
            }
            y -= child->total_height;
            before += child->total_height;
            line += child->line_count;
        }
    }
    {
        size_t i;
        for (i = 0; i < node->count; i++) {
            float height = node->value.leaf.lines[i].height;
            if (y < height || i + 1 == node->count) {
                if (line_y) *line_y = before;
                return line + i;
            }
            y -= height;
            before += height;
        }
    }
    if (line_y) *line_y = before;
    return index->line_count - 1;
}

typedef struct ReplacementLineBuilder {
    TintaEditorLineMetric *lines;
    size_t count;
    size_t current;
    float default_height;
} ReplacementLineBuilder;

static bool replacement_line_feed(const wchar_t *text, size_t length,
                                  void *context) {
    ReplacementLineBuilder *builder = (ReplacementLineBuilder *)context;
    size_t i;
    for (i = 0; i < length; i++) {
        builder->lines[builder->current].length++;
        if (text[i] == L'\n' && builder->current + 1 < builder->count)
            builder->current++;
    }
    return true;
}

static TintaEditorLineMetric *replacement_lines(
    TintaEditorDocument *document, size_t start, size_t end,
    const TintaEditorRopeNode *inserted, size_t *first_line,
    size_t *removed_lines, size_t *inserted_lines,
    float default_height) {
    size_t start_line = line_from_position(&document->lines, start);
    size_t end_line = line_from_position(&document->lines, end);
    size_t start_line_pos;
    size_t end_line_pos;
    TintaEditorLineMetric end_metric;
    TintaEditorLineMetric *result;
    ReplacementLineBuilder builder;
    size_t i;
    if (!tinta_editor_document_get_line(document, start_line,
                                        &start_line_pos, NULL) ||
        !tinta_editor_document_get_line(document, end_line,
                                        &end_line_pos, &end_metric))
        return NULL;
    *first_line = start_line;
    *removed_lines = end_line - start_line + 1;
    *inserted_lines = rope_breaks(inserted) + 1;
    if (*inserted_lines > SIZE_MAX / sizeof(*result)) return NULL;
    result = (TintaEditorLineMetric *)calloc(*inserted_lines, sizeof(*result));
    if (!result) return NULL;
    for (i = 0; i < *inserted_lines; i++) result[i].height = default_height;
    result[0].length = start - start_line_pos;
    memset(&builder, 0, sizeof(builder));
    builder.lines = result;
    builder.count = *inserted_lines;
    builder.default_height = default_height;
    if (!rope_visit(inserted, replacement_line_feed, &builder)) {
        free(result);
        return NULL;
    }
    result[*inserted_lines - 1].length +=
        end_line_pos + end_metric.length - end;
    return result;
}

static void undo_record_destroy(TintaEditorUndoRecord *record) {
    rope_release(record->removed);
    rope_release(record->inserted);
    memset(record, 0, sizeof(*record));
}

static void history_clear(TintaVec *history) {
    size_t i;
    for (i = 0; i < history->len; i++)
        undo_record_destroy(TINTA_VEC_PTR(TintaEditorUndoRecord,
                                          *history, i));
    tinta_vec_clear(history);
}

static void trim_undo(TintaEditorDocument *document) {
    while (document->undo.len && document->undo_bytes > document->undo_limit) {
        TintaEditorUndoRecord *record = TINTA_VEC_PTR(
            TintaEditorUndoRecord, document->undo, 0);
        document->undo_bytes -= record->cost;
        undo_record_destroy(record);
        memmove(document->undo.data,
                (char *)document->undo.data + document->undo.elem_size,
                (document->undo.len - 1) * document->undo.elem_size);
        document->undo.len--;
    }
}

static bool document_apply_rope(TintaEditorDocument *document,
                                size_t start, size_t end,
                                TintaEditorRopeNode *inserted,
                                float default_line_height,
                                TintaEditorRopeNode **removed_out) {
    TintaEditorRopeNode *left = NULL;
    TintaEditorRopeNode *tail = NULL;
    TintaEditorRopeNode *removed = NULL;
    TintaEditorRopeNode *right = NULL;
    TintaEditorRopeNode *middle = NULL;
    TintaEditorRopeNode *new_root = NULL;
    TintaEditorLineMetric *new_lines = NULL;
    size_t first_line = 0;
    size_t removed_lines = 0;
    size_t inserted_lines = 0;
    TintaEditorLinePool pool;
    bool success = false;
    memset(&pool, 0, sizeof(pool));
    new_lines = replacement_lines(document, start, end, inserted,
                                  &first_line, &removed_lines,
                                  &inserted_lines, default_line_height);
    if (!new_lines) goto done;
    if (!line_pool_init(&pool, inserted_lines,
                        line_tree_height(&document->lines)))
        goto done;
    if (!rope_split(document->root, start, &left, &tail)) goto done;
    if (!rope_split(tail, end - start, &removed, &right)) goto done;
    middle = rope_concat(left, inserted);
    if (!middle && (left || inserted)) goto done;
    new_root = rope_concat(middle, right);
    if (!new_root && (middle || right)) goto done;
    if (!line_index_replace(&document->lines, first_line, removed_lines,
                            new_lines, inserted_lines, &pool))
        goto done;
    rope_release(document->root);
    document->root = new_root;
    new_root = NULL;
    document->revision++;
    document->modified = true;
    if (removed_out) {
        *removed_out = removed;
        removed = NULL;
    }
    success = true;
done:
    rope_release(left);
    rope_release(tail);
    rope_release(removed);
    rope_release(right);
    rope_release(middle);
    rope_release(new_root);
    line_pool_destroy(&pool);
    free(new_lines);
    return success;
}

void tinta_editor_document_init(TintaEditorDocument *document,
                                float default_line_height) {
    memset(document, 0, sizeof(*document));
    tinta_vec_init(&document->undo, sizeof(TintaEditorUndoRecord));
    tinta_vec_init(&document->redo, sizeof(TintaEditorUndoRecord));
    document->undo_limit = 64u * 1024u * 1024u;
    document->text_limit = SIZE_MAX;
    line_index_build(&document->lines, NULL, default_line_height);
}

void tinta_editor_document_destroy(TintaEditorDocument *document) {
    if (!document) return;
    history_clear(&document->undo);
    history_clear(&document->redo);
    tinta_vec_destroy(&document->undo);
    tinta_vec_destroy(&document->redo);
    line_index_destroy(&document->lines);
    rope_release(document->root);
    memset(document, 0, sizeof(*document));
}

size_t tinta_editor_document_length(const TintaEditorDocument *document) {
    return document ? rope_length(document->root) : 0;
}

size_t tinta_editor_document_line_count(const TintaEditorDocument *document) {
    return document ? document->lines.line_count : 0;
}

wchar_t tinta_editor_document_char_at(const TintaEditorDocument *document,
                                      size_t position) {
    if (!document || position >= rope_length(document->root)) return 0;
    return rope_char_at(document->root, position);
}

bool tinta_editor_document_copy(const TintaEditorDocument *document,
                                size_t position, size_t length,
                                wchar_t *output) {
    size_t written = 0;
    if (!document || (!output && length) ||
        position > rope_length(document->root) ||
        length > rope_length(document->root) - position)
        return false;
    rope_copy_range(document->root, position, length, output, &written);
    return written == length;
}

bool tinta_editor_document_assign(TintaEditorDocument *document,
                                  const wchar_t *text, size_t length,
                                  bool reject_nul, float default_line_height) {
    TintaEditorRopeNode *root = NULL;
    TintaEditorLineIndex lines = {0};
    if (!document || (!text && length)) return false;
    if (!rope_from_text(text, length, reject_nul, &root)) return false;
    if (rope_length(root) > document->text_limit ||
        !line_index_build(&lines, root, default_line_height)) {
        rope_release(root);
        line_index_destroy(&lines);
        return false;
    }
    rope_release(document->root);
    line_index_destroy(&document->lines);
    document->root = root;
    document->lines = lines;
    document->revision++;
    document->modified = false;
    tinta_editor_document_clear_history(document);
    return true;
}

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
                                   TintaEditorReplaceResult *result) {
    TintaEditorRopeNode *inserted = NULL;
    TintaEditorRopeNode *removed = NULL;
    TintaEditorUndoRecord record;
    size_t inserted_length;
    bool keep_record = false;
    size_t old_length;
    if (!document || start > end) return false;
    old_length = rope_length(document->root);
    if (end > old_length || !rope_from_text(text, length, reject_nul, &inserted))
        return false;
    inserted_length = rope_length(inserted);
    if (inserted_length > document->text_limit - (old_length - (end - start))) {
        rope_release(inserted);
        return false;
    }
    memset(&record, 0, sizeof(record));
    record.position = start;
    record.inserted = inserted;
    record.anchor_before = anchor_before;
    record.caret_before = caret_before;
    record.anchor_after = anchor_after;
    record.caret_after = caret_after;
    if ((end - start) > (SIZE_MAX - inserted_length * sizeof(wchar_t)) /
                            sizeof(wchar_t)) {
        rope_release(inserted);
        return false;
    }
    record.cost = ((end - start) + inserted_length) * sizeof(wchar_t);
    keep_record = record_undo && record.cost <= document->undo_limit;
    if (keep_record &&
        !tinta_vec_reserve(&document->undo, document->undo.len + 1)) {
        rope_release(inserted);
        return false;
    }
    if (!document_apply_rope(document, start, end, inserted,
                             default_line_height, &removed)) {
        rope_release(inserted);
        return false;
    }
    history_clear(&document->redo);
    if (keep_record) {
        record.removed = removed;
        if (!tinta_vec_push(&document->undo, &record)) {
            /* Capacity was reserved above, so this is defensive only. */
            undo_record_destroy(&record);
            tinta_editor_document_clear_history(document);
        } else {
            document->undo_bytes += record.cost;
            trim_undo(document);
        }
    } else {
        rope_release(removed);
        rope_release(inserted);
        if (record_undo) tinta_editor_document_clear_history(document);
    }
    if (result) {
        result->position = start;
        result->removed_length = end - start;
        result->inserted_length = inserted_length;
    }
    return true;
}

size_t tinta_editor_document_line_from_position(
    const TintaEditorDocument *document, size_t position) {
    if (!document) return 0;
    if (position > rope_length(document->root))
        position = rope_length(document->root);
    return line_from_position(&document->lines, position);
}

size_t tinta_editor_document_line_start(
    const TintaEditorDocument *document, size_t line) {
    size_t start = 0;
    if (!document || !line_find_const(&document->lines, line, NULL,
                                      &start, NULL))
        return rope_length(document ? document->root : NULL);
    return start;
}

bool tinta_editor_document_get_line(
    const TintaEditorDocument *document, size_t line,
    size_t *start, TintaEditorLineMetric *metric) {
    size_t offset;
    size_t line_start = 0;
    TintaEditorLineNode *leaf;
    if (!document) return false;
    leaf = line_find_const(&document->lines, line, &offset,
                           &line_start, NULL);
    if (!leaf) return false;
    if (start) *start = line_start;
    if (metric) *metric = leaf->value.leaf.lines[offset];
    return true;
}

bool tinta_editor_document_set_line_metric(
    TintaEditorDocument *document, size_t line, float width, float height,
    uint64_t generation) {
    size_t offset;
    TintaEditorLineNode *leaf;
    if (!document || !isfinite(width) || !isfinite(height) ||
        width < 0 || height <= 0)
        return false;
    leaf = line_find_const(&document->lines, line, &offset, NULL, NULL);
    if (!leaf) return false;
    leaf->value.leaf.lines[offset].width = width;
    leaf->value.leaf.lines[offset].height = height;
    leaf->value.leaf.lines[offset].generation = generation;
    line_recalculate_up(leaf);
    line_index_sync(&document->lines);
    return true;
}

size_t tinta_editor_document_line_from_y(
    const TintaEditorDocument *document, float y, float *line_y) {
    return document ? line_from_height(&document->lines, y, line_y) : 0;
}

float tinta_editor_document_line_y(
    const TintaEditorDocument *document, size_t line) {
    float height = 0;
    if (!document || !line_find_const(&document->lines, line, NULL,
                                      NULL, &height))
        return document ? document->lines.total_height : 0;
    return height;
}

bool tinta_editor_document_can_undo(const TintaEditorDocument *document) {
    return document && document->undo.len != 0;
}

bool tinta_editor_document_can_redo(const TintaEditorDocument *document) {
    return document && document->redo.len != 0;
}

static bool document_history_apply(TintaEditorDocument *document,
                                   TintaVec *source, TintaVec *destination,
                                   bool undo, size_t *anchor, size_t *caret,
                                   float default_line_height) {
    TintaEditorUndoRecord *record;
    size_t remove_length;
    TintaEditorRopeNode *insert;
    if (!source->len || !tinta_vec_reserve(destination, destination->len + 1))
        return false;
    record = TINTA_VEC_PTR(TintaEditorUndoRecord, *source, source->len - 1);
    remove_length = undo ? rope_length(record->inserted) :
                           rope_length(record->removed);
    insert = undo ? record->removed : record->inserted;
    if (!document_apply_rope(document, record->position,
                             record->position + remove_length,
                             insert, default_line_height, NULL))
        return false;
    if (undo) {
        if (anchor) *anchor = record->anchor_before;
        if (caret) *caret = record->caret_before;
        document->undo_bytes -= record->cost;
    } else {
        if (anchor) *anchor = record->anchor_after;
        if (caret) *caret = record->caret_after;
        document->undo_bytes += record->cost;
    }
    tinta_vec_push(destination, record);
    source->len--;
    return true;
}

bool tinta_editor_document_undo(TintaEditorDocument *document,
                                size_t *anchor, size_t *caret,
                                float default_line_height) {
    return document && document_history_apply(document, &document->undo,
        &document->redo, true, anchor, caret, default_line_height);
}

bool tinta_editor_document_redo(TintaEditorDocument *document,
                                size_t *anchor, size_t *caret,
                                float default_line_height) {
    return document && document_history_apply(document, &document->redo,
        &document->undo, false, anchor, caret, default_line_height);
}

void tinta_editor_document_clear_history(TintaEditorDocument *document) {
    if (!document) return;
    history_clear(&document->undo);
    history_clear(&document->redo);
    document->undo_bytes = 0;
}

void tinta_editor_document_set_undo_limit(TintaEditorDocument *document,
                                          size_t bytes) {
    if (!document) return;
    document->undo_limit = bytes;
    trim_undo(document);
}

void tinta_editor_document_coalesce_last(TintaEditorDocument *document,
                                         TintaEditorUndoCoalesce kind) {
    TintaEditorUndoRecord *previous;
    TintaEditorUndoRecord *current;
    TintaEditorRopeNode *combined = NULL;
    size_t previous_inserted;
    size_t current_inserted;
    size_t previous_removed;
    size_t current_removed;
    if (!document || document->undo.len < 2) return;
    previous = TINTA_VEC_PTR(TintaEditorUndoRecord, document->undo,
                             document->undo.len - 2);
    current = TINTA_VEC_PTR(TintaEditorUndoRecord, document->undo,
                            document->undo.len - 1);
    previous_inserted = rope_length(previous->inserted);
    current_inserted = rope_length(current->inserted);
    previous_removed = rope_length(previous->removed);
    current_removed = rope_length(current->removed);
    if (previous->anchor_after != current->anchor_before ||
        previous->caret_after != current->caret_before ||
        previous->anchor_after != previous->caret_after ||
        current->anchor_before != current->caret_before)
        return;
    if (kind == TINTA_EDITOR_UNDO_COALESCE_TYPING) {
        if (previous_removed || current_removed || !previous_inserted ||
            !current_inserted ||
            previous->position + previous_inserted != current->position)
            return;
        combined = rope_concat(previous->inserted, current->inserted);
        if (!combined) return;
        rope_release(previous->inserted);
        previous->inserted = combined;
    } else if (kind == TINTA_EDITOR_UNDO_COALESCE_BACKSPACE) {
        if (previous_inserted || current_inserted || !previous_removed ||
            !current_removed ||
            current->position + current_removed != previous->position)
            return;
        combined = rope_concat(current->removed, previous->removed);
        if (!combined) return;
        rope_release(previous->removed);
        previous->removed = combined;
        previous->position = current->position;
    } else if (kind == TINTA_EDITOR_UNDO_COALESCE_DELETE) {
        if (previous_inserted || current_inserted || !previous_removed ||
            !current_removed || current->position != previous->position)
            return;
        combined = rope_concat(previous->removed, current->removed);
        if (!combined) return;
        rope_release(previous->removed);
        previous->removed = combined;
    } else {
        return;
    }
    previous->anchor_after = current->anchor_after;
    previous->caret_after = current->caret_after;
    previous->cost += current->cost;
    undo_record_destroy(current);
    document->undo.len--;
    trim_undo(document);
}
