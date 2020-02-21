#include "snort_rule_parser.h"
#include <pcre.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Create a new content node */
static struct c_node_t *c_node_new()
{
    c_node_t *node = malloc(sizeof(c_node_t));
    if (node) {
        node->next_content = NULL;
        node->content = NULL;
    }
    return node;
}

/* Create a new pcre node */
static struct pcre_node_t *pcre_node_new()
{
    pcre_node_t *node = malloc(sizeof(pcre_node_t));
    if (node) {
        node->next_pcre_node = NULL;
        node->rule = NULL;
        node->re = NULL;
        node->next = NULL;
    }
    return node;
}

/* Create a new pattern node */
static struct pattern_node_t *pattern_node_new()
{
    pattern_node_t *node = malloc(sizeof(pattern_node_t));
    if (node) {
        node->msg = NULL;
        node->content_node = NULL;
        node->pcre_node = NULL;
    }
    return node;
}

/* Create a new snor_rule node*/
static struct snort_rule *r_node_new()
{
    snort_rule *r_node = malloc(sizeof(snort_rule));
    if (r_node) {
        r_node->action = NULL;
        r_node->protocol = NULL;
        r_node->src_ip = NULL;
        r_node->src_port = NULL;
        r_node->dst_ip = NULL;
        r_node->dst_port = NULL;
        r_node->pattern = NULL;
        r_node->next = NULL;
    }
    return r_node;
}

/* Create a list to store snore_rule nodes */
static struct snort_rule_list *rule_list_create()
{
    snort_rule_list *q = malloc(sizeof(snort_rule));
    if (q) {
        q->head = NULL;
        q->tail = NULL;
        q->length = 0;
    }
    return q;
}

/* Insert a rule_node into rule_list */
static void rule_list_insert(snort_rule_list *q, snort_rule *r_node_new)
{
    if (!q || !r_node_new)
        return;
    else {
        if (!q->head) {
            q->head = r_node_new;
            q->tail = r_node_new;
        } else {
            q->tail->next = r_node_new;
            q->tail = r_node_new;
        }
    }
}

static struct patterns_leaf_t *payload_check_module_new()
{
    patterns_leaf_t *node = malloc(sizeof(patterns_leaf_t));
    if (node) {
        node->ptr = NULL;
        node->msg = NULL;
        node->next = NULL;
    }
    return node;
}

/* Remove space at head and tail */
static char *mystrip(char *str)
{
    char *end;
    end = str + strlen(str) - 1;
    while (str[0] == ' ')
        str++;
    while (end >= str && (end[0] == ' '))
        end--;
    *(end + 1) = '\0';

    return str;
}

/* Find the node which msg is target and return. */
static patterns_leaf_t *find_leaf(char *target, patterns_leaf_t *start)
{
    if (!target)
        return NULL;

    patterns_leaf_t *ret_node = start;
    if (!start) {
        ret_node = payload_check_module_new();
        ret_node->msg = target;
        start = ret_node;
    } else {
        while (strncmp(ret_node->msg, target, strlen(target)) != 0 &&
               ret_node->next) {
            ret_node = ret_node->next;
        }
        if (strncmp(ret_node->msg, target, strlen(target)) != 0) {
            ret_node->next = payload_check_module_new();
            ret_node = ret_node->next;
            ret_node->msg = target;
        }
    }
    return ret_node;
}

static void *find_patterns(char *protocol, char *src_port, char *dst_port)
{
    /* Start at root & check root */
    patterns_leaf_t *location = patterns_root;
    if (!location || strncmp(location->msg, "root", 4) != 0)
        return NULL;

    /* Find protocol node */
    patterns_leaf_t *proto =
        find_leaf(protocol, (patterns_leaf_t *) location->ptr);
    if (location->ptr == NULL) {
        /* printf("Add %s_leaf into the root\n", protocol); */
        location->ptr = proto;
    }

    /* Find src_port node */
    patterns_leaf_t *sp = find_leaf(src_port, (patterns_leaf_t *) proto->ptr);
    if (proto->ptr == NULL) {
        /* printf("Add %s_leaf into the %s_leaf\n", sp->msg, proto->msg); */
        proto->ptr = sp;
    }
    /* Find dst_port node */
    patterns_leaf_t *dp = find_leaf(dst_port, (patterns_leaf_t *) sp->ptr);
    if (sp->ptr == NULL) {
        /* printf("Add %s_leaf into the %s_%s_leaf\n", dp->msg, proto->msg,
         * sp->msg); */
        sp->ptr = dp;
    }
    return dp;
}

static void check_module_add_node(char **buf, pcre_node_t *pcre_node)
{
    /* Find the tcp -> src_port -> dst_port, get the dst_port leaf */
    patterns_leaf_t *leaf = find_patterns(buf[1], buf[3], buf[6]);

    /* dst_port points to the head of a pcre set */
    pcre_node_t *list_head = (pcre_node_t *) leaf->ptr;
    /* if the set is NULL */
    if (list_head == NULL) {
        leaf->ptr = pcre_node;
    } else {
        while (list_head->next != NULL)
            list_head = list_head->next;
        list_head->next = pcre_node;
    }
}

/*
   snort3 rule format
   action protocol src_ip src_port -> dst_ip dst_port (msg:"";xxxx;
   content:"....";pcre:""; )
*/
static void parse_rule(char *str)
{
    /* Create rule node for each line */
    snort_rule *rule_node = r_node_new();
    pattern_node_t *pattern_node = pattern_node_new();

    rule_node->pattern = pattern_node;

    int c = 0;     // count for location of the rule
    char *buf[7];  // store action, protocol, src_ip ... dst_port
    char *space = " ", *semicolon = ";", *delim, *token;

    delim = space;
    token = strtok(str, delim);

    while (token) {
        /* Parse options */
        if (c > 6) {
            delim = semicolon;

            if (token[0] == '(' || token[0] == ')') {
                token = strtok(NULL, delim);
                continue;
            } else if (!strncmp(token, "msg", 3)) {
                /* Remove redudent marks */
                token += 5;
                int i = 0;
                while (token[i] != '"')
                    i++;
                token[i] = '\0';

                /* Copy msg to pattern_node */
                char *str = malloc(sizeof(char) * strlen(token));
                strncpy(str, token, strlen(token));
                pattern_node->msg = str;

            } else if (!strncmp(token, "content:", 7)) {
                /* Remove redudent marks */
                token += 9;
                int i = 0;
                while (token[i] != '"')
                    i++;
                token[i] = '\0';

                /* Create content_node and string */
                c_node_t *content_node = c_node_new();
                char *str = malloc(sizeof(char) * strlen(token));

                /* Copy content string to content_node */
                strncpy(str, token, strlen(token));
                content_node->content = str;

                /* Insert content_node into pattern_node */
                c_node_t *end_node = pattern_node->content_node;
                if (end_node == NULL) {  // There is no content_node
                    pattern_node->content_node = content_node;
                } else {  // There is content_node, find the last one.
                    while (end_node->next_content != NULL)
                        end_node = end_node->next_content;
                    end_node->next_content = content_node;
                }
            } else if (!strncmp(token, "pcre", 4)) {
                /* Remove redudent marks */
                token += 7;
                int i = 0, j = strlen(token);
                while (token[j] != '/')
                    j--;
                token[j] = '\0';

                /* Save rule */
                pcre_node_t *pcre_node = pcre_node_new();
                char *str = malloc(sizeof(char) * (strlen(token) + 1));
                strncpy(str, token, strlen(token) + 1);
                pcre_node->rule = str;

                /* pcre parameters */
                const char *error;
                int erroffset;

                /* pcre compilation */
                pcre_node->re = pcre_compile(str, 0, &error, &erroffset, NULL);
                if (pcre_node->re == NULL) {
                    fprintf(stderr,
                            "PCRE compilation failed at offset %d: %s\n",
                            erroffset, error);
                    fprintf(stderr, "Pattern is: %s\n", str);
                    exit(1);
                }

                /* Insert */
                pcre_node_t *end_node = pattern_node->pcre_node;
                if (end_node == NULL) {
                    pattern_node->pcre_node = pcre_node;
                } else {
                    while (end_node->next_pcre_node != NULL)
                        end_node = end_node->next_pcre_node;
                    end_node->next_pcre_node = pcre_node;
                }
            }
            token = mystrip(strtok(NULL, delim));
        }

        /* Parse action, protocol, srcip... */
        else {
            buf[c] = malloc(sizeof(char) * strlen(token));
            strncpy(buf[c], token, strlen(token));
            token = strtok(NULL, delim);
            c++;
        }
    }

    /* Assign action, protocol, src_ip ... */
    rule_node->action = buf[0];
    rule_node->protocol = buf[1];
    rule_node->src_ip = buf[2];
    rule_node->src_port = buf[3];
    rule_node->dst_ip = buf[5];
    rule_node->dst_port = buf[6];

    /* Insert rule_node into rule_list */
    rule_list_insert(snort_rule_q, rule_node);

    /* Insert pcre_node into check module */
    if (pattern_node->pcre_node)
        check_module_add_node(buf, pattern_node->pcre_node);

    return;
}

static void read_snort_rule()
{
    /* Read snort rule file */
    FILE *fd = fopen("rules/snort3-community.rules", "r");
    char buf[2000];
    int count = 0;

    while (fgets(buf, sizeof(buf), fd)) {
        if (buf[0] == '#' || buf[0] == '\n')
            ;
        else
            parse_rule(buf);
    }
}

static void show_rules()
{
    snort_rule *rule_node = snort_rule_q->head;

    while (rule_node) {
        /* Show action, protocol, ... , dst_port */
        printf("%s %s %s %s %s %s ", rule_node->action, rule_node->protocol,
               rule_node->src_ip, rule_node->src_port, rule_node->dst_ip,
               rule_node->dst_port);
        printf("Alert msg: \"%s\" ", rule_node->pattern->msg);

        /* Show content */
        c_node_t *cn = rule_node->pattern->content_node;
        if (cn != NULL) {
            printf("Content:\"%s", cn->content);
            while (cn->next_content != NULL) {
                cn = cn->next_content;
                printf(" -> %s", cn->content);
            }
            printf("\" ");
        }

        /* Show pcre */
        if (rule_node->pattern->pcre_node)
            printf("pcre:\"%s", rule_node->pattern->pcre_node->rule);

        printf("\n");
        rule_node = rule_node->next;
    }
}

bool patterns_tree_init()
{
    patterns_root = payload_check_module_new();

    if (patterns_root) {
        char *msg = malloc(sizeof(char *));
        strcpy(msg, "root");
        patterns_root->msg = msg;

        return true;
    }
    return false;
}

void snort_rule_init()
{
    /* Parameters declaration. */
    snort_rule_q = rule_list_create();
    int ret = patterns_tree_init();
    if (!ret)
        fprintf(stderr, "payload check module initialization error!\n");

    /* Read rule file & parse. */
    read_snort_rule();

    return;
}

void snort_rule_release(snort_rule_list *q)
{
    snort_rule *r_node = q->head;
    while (r_node) {
        snort_rule *next = r_node->next;
        free(r_node);
        r_node = next;
    }
    return;
}
