#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <libgen.h>

/* Helper to normalize media paths like src="/foo.png" to src="media/foo.png" */
void print_normalized_content(FILE *out, const char *line) {
    const char *p = line;
    while (*p) {
        if (strncmp(p, "src=\"/", 6) == 0) {
            fprintf(out, "src=\"media/");
            p += 6;
        } else if (strncmp(p, "src='/", 6) == 0) {
            fprintf(out, "src='media/");
            p += 6;
        } else if (strncmp(p, "href=\"/", 7) == 0) {
            fprintf(out, "href=\"media/");
            p += 7;
        } else if (strncmp(p, "href='/", 7) == 0) {
            fprintf(out, "href='media/");
            p += 7;
        } else {
            fputc(*p, out);
            p++;
        }
    }
    fputc('\n', out);
}

/* Helper to escape double quotes in HTML attributes */
void print_escaped_id(FILE *out, const char *s) {
    while (*s) {
        if (*s == '"') fprintf(out, "&quot;");
        else fputc(*s, out);
        s++;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.txt.html [output_dir]\n", argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    char *output_dir = (argc > 2) ? argv[2] : ".";

    FILE *in = fopen(input_path, "r");
    if (!in) {
        perror("Error opening input file");
        return 1;
    }

    /* Create output directory if it doesn't exist */
    struct stat st = {0};
    if (stat(output_dir, &st) == -1) {
        #ifdef _WIN32
        mkdir(output_dir);
        #else
        mkdir(output_dir, 0755);
        #endif
    }

    char *base_name = strdup(input_path);
    char *stem = basename(base_name);
    char *dot = strstr(stem, ".txt.html");
    if (dot) *dot = '\0';
    else {
        dot = strrchr(stem, '.');
        if (dot) *dot = '\0';
    }

    char html_path[1024];
    snprintf(html_path, sizeof(html_path), "%s/%s.html", output_dir, stem);

    FILE *out = fopen(html_path, "w");
    if (!out) {
        perror("Error opening output file");
        free(base_name);
        fclose(in);
        return 1;
    }

    fprintf(stderr, "Converting %s -> %s\n", input_path, html_path);

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    char *headword = NULL;
    
    int state = 0; /* 0: waiting for headword, 1: reading content */

    while ((read = getline(&line, &len, in)) != -1) {
        /* Remove trailing newline */
        if (read > 0 && line[read - 1] == '\n') {
            line[read - 1] = '\0';
            read--;
        }
        if (read > 0 && line[read - 1] == '\r') {
            line[read - 1] = '\0';
            read--;
        }

        if (state == 0) {
            if (read == 0) continue;
            if (strcmp(line, "</>") == 0) continue;

            headword = strdup(line);
            fprintf(out, "<article id=\"");
            print_escaped_id(out, headword);
            fprintf(out, "\" class=\"entry\">\n");
            state = 1;
        } else {
            if (strcmp(line, "</>") == 0) {
                fprintf(out, "</article>\n");
                free(headword);
                headword = NULL;
                state = 0;
            } else {
                if (strncmp(line, "@@@LINK=", 8) == 0) {
                    fprintf(out, "<a href=\"entry://%s\">%s</a>\n", line + 8, line);
                } else {
                    print_normalized_content(out, line);
                }
            }
        }
    }

    if (state == 1) {
        fprintf(out, "</article>\n");
        free(headword);
    }

    fclose(out);
    fclose(in);
    free(line);

    /* Generate meta.json */
    char meta_path[1024];
    snprintf(meta_path, sizeof(meta_path), "%s/meta.json", output_dir);
    FILE *meta = fopen(meta_path, "w");
    if (meta) {
        fprintf(meta, "{\n");
        fprintf(meta, "  \"format\": 1,\n");
        fprintf(meta, "  \"id\": \"%s\",\n", stem);
        fprintf(meta, "  \"name\": \"%s\",\n", stem);
        fprintf(meta, "  \"short_name\": \"%s\",\n", stem);
        fprintf(meta, "  \"index_languages\": [\"en\"],\n");
        fprintf(meta, "  \"content_languages\": [\"en\"],\n");
        fprintf(meta, "  \"version\": \"1.0\",\n");
        fprintf(meta, "  \"stylesheet\": \"style.css\",\n");
        fprintf(meta, "  \"html\": \"%s.html\"\n", stem);
        fprintf(meta, "}\n");
        fclose(meta);
    }

    /* Create an empty style.css if it doesn't exist */
    char css_path[1024];
    snprintf(css_path, sizeof(css_path), "%s/style.css", output_dir);
    FILE *css = fopen(css_path, "a");
    if (css) fclose(css);

    fprintf(stderr, "Done.\n");

    free(base_name);
    return 0;
}
