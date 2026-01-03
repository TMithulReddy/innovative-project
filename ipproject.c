/* =========================================================================
   Knowledge Graph Engine (C, Single-File Edition)
   -------------------------------------------------------------------------
 ->  Purpose:
     A robust, user-friendly knowledge graph system using core data structures:
       - Hash Table (for O(1) entity lookup)
       - Adjacency List via Linked Lists (for directed relations)
       - Queue (BFS path finding)
->   Build & Run:
     gcc -o knowledge_graph knowledge_graph.c
     ./knowledge_graph
->  Optional (to render PNG after exporting .dot):
     dot -Tpng kg_graph.dot -o graph.png

   ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* [SECTION] Configuration & UI Constants */

#define HASH_SIZE   101          
#define NAME_LEN    128
#define REL_LEN     128
#define LINE_BUF    512
#define SUGGEST_MAX 16          
#define QUEUE_INIT  128          

#define DEFAULT_DATA_FILE  "relations.txt"
#define DEFAULT_DOT_FILE   "kg_graph.dot"

/* ANSI colors for a clean, professional console UI */
#define RESET   "\033[0m"
#define CYAN    "\033[1;36m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define RED     "\033[1;31m"
#define BLUE    "\033[1;34m"
#define MAGENTA "\033[1;35m"
#define WHITE   "\033[1;37m"

/* =========================================================================
   [SECTION] Data Structures
   - Relation: labeled directed edge to a target entity
   - Entity: node with name, adjacency list head, hash-chain link
   ========================================================================= */
typedef struct Entity Entity;

typedef struct Relation {
    char rel[REL_LEN];
    Entity *target;
    struct Relation *next;   /* adjacency next */
} Relation;

struct Entity {
    char name[NAME_LEN];
    Relation *relations;     /* adjacency list head */
    struct Entity *hnext;    /* hash chain next */
    /* transient fields for BFS */
    int visited;
    Entity *prev;
};

/* Global hash table (closed chaining) */
static Entity *gHash[HASH_SIZE] = { NULL };

/*  [SECTION] Utility: Safe I/O, String Helpers, Trimming, Case, etc. */

/* Read a line safely, strip trailing newline. */
static void read_line(char *buf, size_t n) {
    if (!fgets(buf, (int)n, stdin)) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\r\n")] = 0;
}

/* Trim leading & trailing whitespace (in place). */
static void trim(char *s) {
    size_t len = strlen(s);
    size_t start = 0, end = len ? len - 1 : 0;

    while (s[start] && isspace((unsigned char)s[start])) start++;
    while (end > start && isspace((unsigned char)s[end])) end--;

    if (start > 0) memmove(s, s + start, end - start + 1);
    s[end - start + 1] = '\0';
}

/* Optional normalization: collapse multiple internal spaces into single space. */
static void squeeze_spaces(char *s) {
    char *dst = s;
    int in_space = 0;
    for (; *s; ++s) {
        if (isspace((unsigned char)*s)) {
            if (!in_space) { *dst++ = ' '; in_space = 1; }
        } else {
            *dst++ = *s; in_space = 0;
        }
    }
    *dst = '\0';
}

/* Lowercase copy (safe). */
static void to_lower_copy(const char *src, char *dst, size_t dstsz) {
    size_t i = 0;
    for (; src[i] && i + 1 < dstsz; ++i) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

/* Case-insensitive compare. Returns 0 if equal ignoring case. */
static int ci_cmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        ++a; ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Case-insensitive substring check. */
static int ci_contains(const char *hay, const char *needle) {
    char H[NAME_LEN*2], N[NAME_LEN];
    to_lower_copy(hay, H, sizeof(H));
    to_lower_copy(needle, N, sizeof(N));
    return strstr(H, N) != NULL;
}

/* [SECTION] Hash Table Operations */
static unsigned hash_index(const char *s) {
    /* djb2 */
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*s++)) h = ((h << 5) + h) + (unsigned)c;
    return (unsigned)(h % HASH_SIZE);
}

static Entity* find_entity_exact(const char *name) {
    unsigned idx = hash_index(name);
    for (Entity *e = gHash[idx]; e; e = e->hnext)
        if (strcmp(e->name, name) == 0) return e;
    return NULL;
}

static Entity* create_entity(const char *name) {
    Entity *e = (Entity*)malloc(sizeof(Entity));
    if (!e) { printf(RED "Memory allocation failed\n" RESET); exit(1); }
    strncpy(e->name, name, NAME_LEN-1); e->name[NAME_LEN-1] = '\0';
    e->relations = NULL;
    e->visited = 0;
    e->prev = NULL;

    unsigned idx = hash_index(e->name);
    e->hnext = gHash[idx];
    gHash[idx] = e;
    return e;
}

static Entity* get_or_create_entity(const char *name) {
    Entity *e = find_entity_exact(name);
    if (e) return e;
    return create_entity(name);
}

/*[SECTION] Graph Operations (Edges/Relations) */

static void add_relationship(const char *src, const char *rel, const char *tgt) {
    Entity *S = get_or_create_entity(src);
    Entity *T = get_or_create_entity(tgt);
    Relation *R = (Relation*)malloc(sizeof(Relation));
    if (!R) { printf(RED "Memory allocation failed\n" RESET); exit(1); }
    strncpy(R->rel, rel, REL_LEN-1); R->rel[REL_LEN-1] = '\0';
    R->target = T;
    R->next = S->relations;
    S->relations = R;

    printf(GREEN "✔ Added: " CYAN "\"%s\"" RESET " --" WHITE "%s" RESET "--> " CYAN "\"%s\"" RESET "\n",
           S->name, R->rel, T->name);
}

/*
   [SECTION] Fuzzy Search (Case-insensitive + Prefix/Substring Suggestions)
   - Returns an Entity* after disambiguation, or NULL if no match.
    */

static Entity* fuzzy_pick_from_suggestions(Entity **list, int count) {
    if (count <= 0) return NULL;
    if (count == 1) return list[0];

    printf(YELLOW "\nDid you mean:\n" RESET);
    for (int i = 0; i < count; ++i) {
        printf("  %2d) %s\n", i + 1, list[i]->name);
    }
    printf(WHITE "Choose (1-%d) or 0 to cancel: " RESET, count);

    char buf[32]; read_line(buf, sizeof(buf));
    int choice = atoi(buf);
    if (choice >= 1 && choice <= count) return list[choice - 1];
    printf(RED "Cancelled selection.\n" RESET);
    return NULL;
}

/* Main fuzzy search:
   1) exact (case-insensitive) match
   2) prefix matches
   3) substring matches
*/
static Entity* search_entity_smart(const char *user_input) {
    char key[NAME_LEN]; strncpy(key, user_input, NAME_LEN-1); key[NAME_LEN-1] = '\0';
    trim(key); squeeze_spaces(key);
    if (key[0] == '\0') return NULL;

    /* Pass 1: exact (case-insensitive) */
    for (int i = 0; i < HASH_SIZE; ++i) {
        for (Entity *e = gHash[i]; e; e = e->hnext) {
            if (ci_cmp(e->name, key) == 0) return e;
        }
    }

    /* Collect suggestions (prefix first) */
    Entity *sugg[SUGGEST_MAX]; int sc = 0;

    /* Pass 2: prefix (case-insensitive) */
    for (int i = 0; i < HASH_SIZE && sc < SUGGEST_MAX; ++i) {
        for (Entity *e = gHash[i]; e && sc < SUGGEST_MAX; e = e->hnext) {
            char lowE[NAME_LEN], lowK[NAME_LEN];
            to_lower_copy(e->name, lowE, sizeof(lowE));
            to_lower_copy(key,    lowK, sizeof(lowK));
            if (strncmp(lowE, lowK, strlen(lowK)) == 0) {
                sugg[sc++] = e;
            }
        }
    }

    /* Pass 3: substring (case-insensitive) */
    if (sc == 0) {
        for (int i = 0; i < HASH_SIZE && sc < SUGGEST_MAX; ++i) {
            for (Entity *e = gHash[i]; e && sc < SUGGEST_MAX; e = e->hnext) {
                if (ci_contains(e->name, key)) {
                    sugg[sc++] = e;
                }
            }
        }
    }

    return fuzzy_pick_from_suggestions(sugg, sc);
}

/* 
   [SECTION] BFS Path Finding (prints a clean path if found)
  */
static void reset_bfs_marks(void) {
    for (int i = 0; i < HASH_SIZE; ++i)
        for (Entity *e = gHash[i]; e; e = e->hnext)
            e->visited = 0, e->prev = NULL;
}

static void find_path_bfs(const char *src_in, const char *tgt_in, int fuzzy) {
    Entity *src = fuzzy ? search_entity_smart(src_in) : find_entity_exact(src_in);
    Entity *tgt = fuzzy ? search_entity_smart(tgt_in) : find_entity_exact(tgt_in);

    if (!src) { printf(RED "✖ Source not found.\n" RESET); return; }
    if (!tgt) { printf(RED "✖ Target not found.\n" RESET); return; }

    reset_bfs_marks();

    int cap = QUEUE_INIT, head = 0, tail = 0;
    Entity **Q = (Entity**)malloc(sizeof(Entity*) * cap);
    if (!Q) { printf(RED "Memory allocation failed\n" RESET); exit(1); }

    src->visited = 1; Q[tail++] = src;
    int found = 0;

    while (head < tail) {
        if (tail >= cap) { cap *= 2; Q = (Entity**)realloc(Q, sizeof(Entity*) * cap); }
        Entity *cur = Q[head++];
        if (cur == tgt) { found = 1; break; }

        for (Relation *r = cur->relations; r; r = r->next) {
            Entity *n = r->target;
            if (!n->visited) {
                n->visited = 1;
                n->prev = cur;
                Q[tail++] = n;
            }
        }
    }

    if (!found) {
        printf(RED "\n✖ No path found from \"%s\" to \"%s\".\n" RESET, src->name, tgt->name);
        free(Q); return;
    }

    /* Reconstruct path (reverse via prev) */
    Entity *stack[1024]; int top = 0;
    for (Entity *p = tgt; p; p = p->prev) stack[top++] = p;

    printf(GREEN "\n🧭 Path Found:\n" RESET);
    for (int i = top - 1; i >= 0; --i) {
        printf(CYAN "%s" RESET, stack[i]->name);
        if (i) printf(WHITE " -> " RESET);
    }
    printf("\n");

    free(Q);
}

/*
   [SECTION] Display: Advanced, Neat UI Blocks
 */
static void banner(void) {
    printf(CYAN "\n╔═══════════════════════════════════════════════╗\n");
    printf("║         🧠 KNOWLEDGE GRAPH ENGINE             ║\n");
    printf("╚═══════════════════════════════════════════════╝\n" RESET);
}

static void menu(void) {
    printf(BLUE "\n[ MENU ]" RESET "\n");
    printf(GREEN "1." RESET " ➕ Add Entity (manual)\n");
    printf(GREEN "2." RESET " 🔗 Add Relationship (manual)\n");
    printf(GREEN "3." RESET " 📋 Display Connections (fuzzy)\n");
    printf(GREEN "4." RESET " 🧭 Find Connection Path (BFS + fuzzy)\n");
    printf(GREEN "5." RESET " 📂 Load Graph from File (batch)\n");
    printf(GREEN "6." RESET " 🗂️  Batch Input (N lines: src|rel|tgt)\n");
    printf(GREEN "7." RESET " 💾 Save Graph to File\n");
    printf(GREEN "8." RESET " 🖼️  Export Graph to DOT (.dot for PNG)\n");
    printf(GREEN "9." RESET " 🚪 Exit\n");
    printf(WHITE "Enter choice: " RESET);
}

static void display_connections(const char *query, int fuzzy) {
    Entity *e = fuzzy ? search_entity_smart(query) : find_entity_exact(query);
    if (!e) { printf(RED "✖ Entity not found.\n" RESET); return; }

    printf("\n" BLUE "═══════════════════════════════════════════\n" RESET);
    printf(MAGENTA "  🔗 CONNECTIONS OF: %s\n" RESET, e->name);
    printf(BLUE "═══════════════════════════════════════════\n" RESET);

    Relation *r = e->relations;
    if (!r) { printf(YELLOW "   (No outgoing relationships)\n" RESET); return; }

    printf(WHITE "   %-28s | %-28s\n" RESET, "Target Entity", "Relationship");
    printf(BLUE  "   --------------------------------------------------------\n" RESET);
    for (; r; r = r->next) {
        printf("   %-28s | %-28s\n", r->target->name, r->rel);
    }
    printf(BLUE "═══════════════════════════════════════════\n" RESET);
}

/* 
   [SECTION] File I/O: Robust Load & Save
   - Format per line:  Source|Relationship|Target
   - Skips: blanks, lines starting with '#'
   - Trims & normalizes spaces around tokens
 */

static int parse_relation_line(char *line, char *src, char *rel, char *tgt) {
    /* Expect exactly two '|' separators */
    char *p1 = strchr(line, '|');
    if (!p1) return 0;
    char *p2 = strchr(p1 + 1, '|');
    if (!p2) return 0;

    /* Extract substrings */
    size_t L1 = (size_t)(p1 - line);
    size_t L2 = (size_t)(p2 - (p1 + 1));
    size_t L3 = strlen(p2 + 1);

    if (L1 == 0 || L2 == 0 || L3 == 0) return 0;

    if (L1 >= NAME_LEN) L1 = NAME_LEN - 1;
    if (L2 >= REL_LEN)  L2 = REL_LEN - 1;
    if (L3 >= NAME_LEN) L3 = NAME_LEN - 1;

    strncpy(src, line, L1); src[L1] = '\0';
    strncpy(rel, p1 + 1, L2); rel[L2] = '\0';
    strncpy(tgt, p2 + 1, L3); tgt[L3] = '\0';

    trim(src); trim(rel); trim(tgt);
    squeeze_spaces(src); squeeze_spaces(rel); squeeze_spaces(tgt);
    return 1;
}

static void load_from_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { printf(RED "✖ Cannot open '%s'\n" RESET, filename); return; }

    char line[LINE_BUF];
    int count = 0, bad = 0, lineNo = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineNo++;
        line[strcspn(line, "\r\n")] = 0;
        trim(line);
        if (line[0] == '\0') continue;      /* skip blanks */
        if (line[0] == '#')  continue;      /* skip comments */

        char src[NAME_LEN], rel[REL_LEN], tgt[NAME_LEN];
        if (!parse_relation_line(line, src, rel, tgt)) {
            bad++; 
            printf(YELLOW "⚠ Skipping invalid line %d: \"%s\"\n" RESET, lineNo, line);
            continue;
        }
        add_relationship(src, rel, tgt);
        count++;
    }
    fclose(fp);
    printf(GREEN "📂 Loaded %d relations from '%s' (skipped %d)\n" RESET, count, filename, bad);
}

static void save_to_file(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) { printf(RED "✖ Cannot write '%s'\n" RESET, filename); return; }

    for (int i = 0; i < HASH_SIZE; ++i) {
        for (Entity *e = gHash[i]; e; e = e->hnext) {
            for (Relation *r = e->relations; r; r = r->next) {
                fprintf(fp, "%s|%s|%s\n", e->name, r->rel, r->target->name);
            }
        }
    }
    fclose(fp);
    printf(GREEN "💾 Saved graph to '%s'\n" RESET, filename);
}

/* 
   [SECTION] Batch Input (manual, N lines)
  */
static void batch_input_lines(int n) {
    char line[LINE_BUF];
    for (int i = 1; i <= n; ++i) {
        printf(WHITE "Line %d [src|rel|tgt]: " RESET, i);
        read_line(line, sizeof(line));
        trim(line);
        if (line[0] == '\0' || line[0] == '#') { 
            printf(YELLOW "  (skipped)\n" RESET); 
            continue; 
        }
        char src[NAME_LEN], rel[REL_LEN], tgt[NAME_LEN];
        if (!parse_relation_line(line, src, rel, tgt)) {
            printf(RED "  Invalid format. Use: Source|Relationship|Target\n" RESET);
            --i; /* re-ask same line index */
            continue;
        }
        add_relationship(src, rel, tgt);
    }
}

/* 
   [SECTION] GraphViz .dot Export (for PNG rendering externally)
*/
static void export_dot(const char *dotfile) {
    FILE *fp = fopen(dotfile, "w");
    if (!fp) { 
        printf(RED "✖ Cannot create '%s'\n" RESET, dotfile); 
        return; 
    }

    // Modern GraphViz Styling
    fprintf(fp, "digraph KnowledgeGraph {\n");
    fprintf(fp, "  rankdir=LR;\n"); 
    fprintf(fp, "  layout=dot;\n");
    fprintf(fp, "  graph [splines=true, overlap=false, ranksep=1.3, nodesep=1.0, fontsize=12, fontname=\"Calibri\", bgcolor=\"#FFFFFF\"];\n");

    // Node Style (Soft Blue | Rounded Box | Drop Shadow-ish contrast)
    fprintf(fp, "  node [shape=box, style=filled, fontname=\"Calibri\", fontsize=11, penwidth=1.5, "
                "color=\"#1A73E8\", fillcolor=\"#E8F0FE\", fontcolor=\"#202124\"];\n");
    // Edge Style (Smooth dark gray arrows with nice labels)
    fprintf(fp, "  edge [color=\"#5F6368\", fontname=\"Calibri\", fontsize=10, penwidth=1.3, arrowsize=0.85, fontcolor=\"#3C4043\"];\n\n");

    // Entities & Relations Output
    for (int i = 0; i < HASH_SIZE; ++i) {
        for (Entity *e = gHash[i]; e; e = e->hnext) {
            if (!e->relations) {
                fprintf(fp, "  \"%s\";\n", e->name);
            }
            for (Relation *r = e->relations; r; r = r->next) {
                fprintf(fp,
                    "  \"%s\" -> \"%s\" [label=\"%s\"];\n",
                    e->name, r->target->name, r->rel
                );
            }
        }
    }

    fprintf(fp, "}\n");
    fclose(fp);

    printf(GREEN "\n✅ Modern DOT file exported to '%s'\n" RESET, dotfile);
    printf(WHITE "To render a high-quality PNG run:\n" RESET CYAN
        "  dot -Tpng -Gdpi=300 %s -o graph_hd.png\n" RESET, dotfile);
    printf(YELLOW "Tip: Also try:\n"
           "  dot -Kneato -Tpng %s -o graph_layout2.png\n"
           "  dot -Kfdp -Tpng %s -o graph_layout3.png\n\n" RESET,
           dotfile, dotfile);
}


/*
   [SECTION] Memory Cleanup
 */
static void free_graph(void) {
    for (int i = 0; i < HASH_SIZE; ++i) {
        Entity *e = gHash[i];
        while (e) {
            Relation *r = e->relations;
            while (r) { Relation *tmp = r; r = r->next; free(tmp); }
            Entity *tmpE = e; e = e->hnext; free(tmpE);
        }
        gHash[i] = NULL;
    }
}

/* 
   [SECTION] Main Program Loop (UI, Navigation)
 */
int main(void) {
    banner();

    char buf[LINE_BUF];
    int choice;

    for (;;) {
        menu();
        read_line(buf, sizeof(buf));
        choice = atoi(buf);

        if (choice == 1) { /* Add Entity (manual) */
            printf(WHITE "Enter entity name: " RESET);
            read_line(buf, sizeof(buf));
            trim(buf); squeeze_spaces(buf);
            if (buf[0] == '\0') { printf(YELLOW "⚠ Empty name. Skipped.\n" RESET); continue; }
            if (find_entity_exact(buf)) {
                printf(YELLOW "⚠ '%s' already exists.\n" RESET, buf);
            } else {
                create_entity(buf);
                printf(GREEN "✔ Entity '%s' added.\n" RESET, buf);
            }
        }
        else if (choice == 2) { /* Add Relationship (manual) */
            char s[NAME_LEN], r[REL_LEN], t[NAME_LEN];

            printf(WHITE "Source entity          : " RESET); read_line(s, sizeof(s)); trim(s); squeeze_spaces(s);
            printf(WHITE "Relationship (label)   : " RESET); read_line(r, sizeof(r)); trim(r); squeeze_spaces(r);
            printf(WHITE "Target entity          : " RESET); read_line(t, sizeof(t)); trim(t); squeeze_spaces(t);

            if (s[0] == '\0' || r[0] == '\0' || t[0] == '\0') {
                printf(RED "✖ Invalid input. All fields are required.\n" RESET);
                continue;
            }
            add_relationship(s, r, t);
        }
        else if (choice == 3) { /* Display Connections (fuzzy) */
            printf(WHITE "Enter entity to view: " RESET);
            read_line(buf, sizeof(buf));
            display_connections(buf, /*fuzzy*/1);
        }
        else if (choice == 4) { /* Find Path (BFS + fuzzy) */
            char s[NAME_LEN], t[NAME_LEN];
            printf(WHITE "Enter source entity: " RESET); read_line(s, sizeof(s));
            printf(WHITE "Enter target entity: " RESET); read_line(t, sizeof(t));
            find_path_bfs(s, t, /*fuzzy*/1);
        }
        else if (choice == 5) { /* Load from File */
            printf(WHITE "Enter filename (Enter for default: %s): " RESET, DEFAULT_DATA_FILE);
            read_line(buf, sizeof(buf));
            if (buf[0] == '\0') strcpy(buf, DEFAULT_DATA_FILE);
            load_from_file(buf);
        }
        else if (choice == 6) { /* Batch Input (N lines) */
            printf(WHITE "How many lines (src|rel|tgt)? " RESET);
            read_line(buf, sizeof(buf));
            int n = atoi(buf);
            if (n <= 0) { printf(YELLOW "⚠ Nothing to do.\n" RESET); continue; }
            batch_input_lines(n);
        }
        else if (choice == 7) { /* Save to File */
            printf(WHITE "Enter filename (Enter for default: %s): " RESET, DEFAULT_DATA_FILE);
            read_line(buf, sizeof(buf));
            if (buf[0] == '\0') strcpy(buf, DEFAULT_DATA_FILE);
            save_to_file(buf);
        }
        else if (choice == 8) { /* Export to DOT */
            printf(WHITE "Enter DOT filename (Enter for default: %s): " RESET, DEFAULT_DOT_FILE);
            read_line(buf, sizeof(buf));
            if (buf[0] == '\0') strcpy(buf, DEFAULT_DOT_FILE);
            export_dot(buf);
        }
        else if (choice == 9) { /* Exit */
            printf(MAGENTA "\n🚀 Exiting Knowledge Graph Engine... Goodbye!\n" RESET);
            free_graph();
            break;
        }
        else {
            printf(RED "Invalid choice. Please try again.\n" RESET);
        }
    }
    return 0;
}
