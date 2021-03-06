/* symtab.c
 * Functions for the symbol table.
 * Written by THC for CS 57.
 *
 * extended and changed by sws
 *
 * You should extend the functions as appropriate.
 *
 * Students: Yondon Fu and Matt McFarland - Delights (CS57 16W)
 */


#include "symtab.h"
#include "ast.h"
#include "ast_stack.h"
#include "temp_list.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#define INIT_STK_SIZE 10
#define ACTIVATION_OFFSET 8

static const int HASHSIZE = 211;

/*
 * traverses an AST parse tree and completes symbol
 */
void traverse_ast_tree(ast_node root, symboltable_t * symtab) {

  assert(symtab);

  if (root == NULL)
    return;

  ASTPush(root, symtab->leaf->scopeStack);

  /* attach root's scope to current scope */
  root->scope_table = symtab->leaf;

  switch(root->node_type) {
  case FUNC_DECLARATION_N:
    // handle function node declaration and skip to first child of function compound statement
    for (ast_node child = handle_func_decl_node(root,symtab); child != NULL; 
          child = child->right_sibling) {
      traverse_ast_tree(child, symtab);
    }
      
    break;

  case VAR_DECLARATION_N:
    add_scope_to_children(root, symtab);
    handle_var_decl_line_node(root,symtab);
    // don't traverse these children
    // can return now because parent call will move onto sibling
    break;

  case COMPOUND_STMT_N:
    if (root->left_child != NULL)
      enter_scope(symtab, root, "BLOCK");

    for (ast_node child = root->left_child; child != NULL; child = child->right_sibling)
      traverse_ast_tree(child, symtab);

    break;

  default:
    for (ast_node child = root->left_child; child != NULL; child = child->right_sibling)
      traverse_ast_tree(child, symtab);

    break;  
  }

  ast_node popped = ASTPop(symtab->leaf->scopeStack);

  if (ASTSize(symtab->leaf->scopeStack) == 0 && root->right_sibling == NULL)
    leave_scope(symtab);
  
  return;
}


/*
 * Handles adding function declarations to symbol table. changes scope and add 
 * new parameters to new scope. Returns next sibling in compound statement to tranverse.
 */
ast_node handle_func_decl_node(ast_node fdl, symboltable_t * symtab) {

  assert(fdl);
  assert(symtab);

  // Insert symnode for function into leaf symhashtable
  symnode_t *fdl_node = insert_into_symboltable(symtab, fdl->left_child->right_sibling->value_string, fdl);
  if (fdl_node == NULL) {
    /* duplicate symbol in scope */
    fprintf(stderr, "error: duplicate symbol \'%s\' found. Please fix before continuing.\n", fdl->left_child->right_sibling->value_string);
    exit(1);
  }

  // add scope to all current scope children -- type specifer and ID_T and compound statement
  // formal params should be added to new scope which is done after enter_new scope is called
  add_scope_to_children(fdl->left_child, symtab);
  add_scope_to_children(fdl->left_child->right_sibling, symtab);
  fdl->left_child->right_sibling->right_sibling->right_sibling->scope_table = symtab->leaf;

  // Set symnode as a func node
  set_node_type(fdl_node, FUNC_SYM);

  // Get return type
  type_specifier_t return_type = get_datatype(fdl->left_child->left_child);

  // Get id specifier
  char *id = fdl->left_child->right_sibling->value_string;     // should strdup this for the symbol table?
  
  // Count arguments
  ast_node arg_params = fdl->left_child->right_sibling->right_sibling;
  ast_node arg = arg_params->left_child;

  int arg_count = 0;
  var_symbol * arg_arr = NULL;

  /* collect all argument parameters */
  if (arg->node_type != VOID_N) {

    int arg_arr_size = 5;     /* magic number */
    arg_arr = (var_symbol *)calloc(arg_arr_size, sizeof(var_symbol));
    assert(arg_arr);
   
    type_specifier_t type;
    modifier_t mod;
    int byte_size;
    char * name;

    int param_offset = TYPE_SIZE(INT_TS);
    int argument_offset = 0;
    while (arg != NULL) {

      /* make a variable for each argument */
      type = get_datatype(arg->left_child->left_child);
      if (arg->node_type == FORMAL_PARAM_N) {
        mod = SINGLE_DT;
        byte_size = 4;
      } else {
        mod = ARRAY_DT;
        byte_size = 4 * arg->value_int;
      }

      name = arg->left_child->right_sibling->value_string;

      // calculate change in stack pointer -- note: push down to avoid inserting now
      arg_arr[arg_count] = init_variable(name, type, mod, PARAMETER_VAR, byte_size);   // static variable on stack
      //arg_arr[arg_count].offset_of_frame_pointer = param_offset;            // need to push offsets now b/c order matters
      //param_offset += TYPE_SIZE(type);                                      // keep going

      arg_count++;

      /* resize type array */
      if (arg_count == arg_arr_size) {
        arg_arr_size *= 2;
        arg_arr = realloc(arg_arr, sizeof(var_symbol) * arg_arr_size);
        assert(arg_arr);
      }

      /* move to next argument */
      arg = arg->right_sibling;
    }
  }

  /* set parameter offsets of frame pointer. First parameter needs to be highest off of FP */
  int param_offset = ACTIVATION_OFFSET;
  for (int i = arg_count - 1; i >= 0; i--) {
    arg_arr[i].offset_of_frame_pointer = param_offset;
    param_offset += TYPE_SIZE(arg_arr[i].type);
  }

  /* add function symbol to current (global) scope */

  // Set fields for func node in current (global) scope
  set_node_func(fdl_node, id, return_type, arg_count, arg_arr);

  /* get CS node */
  ast_node compound_stmt = fdl->left_child->right_sibling->right_sibling->right_sibling;
  ast_node next_node = NULL;

  if (compound_stmt->left_child != NULL) {

    /* enter new scope -- skipping CS node */
    enter_scope(symtab, compound_stmt->left_child, fdl_node->name);

    /* set function node as owner of scope and all subscopes */
    symtab->leaf->function_owner = fdl_node;

    /* give function body a new temp list */
    symtab->leaf->t_list = init_temp_list();

    /* add scope to all argument parameter children */
    add_scope_to_children(arg_params, symtab);
    symhashtable_t * symhashtab = symtab->leaf;

    /* add function parameters to this new scope symbol table */
    for (int i = 0; i < arg_count; i++) {

      // Insert var node in leaf symhashtable
      symnode_t *var_node = insert_into_symboltable(symtab, (&arg_arr[i])->name, fdl);

      if (var_node == NULL) {
        fprintf(stderr, "error: duplicate variable symbol \'%s\' found. Please fix before continuing.\n", (&arg_arr[i])->name);
        exit(1);
      }

      set_node_type(var_node, VAR_SYM);
      set_node_var(var_node, &arg_arr[i]);

      /* set offset */
      var_node->s.v.offset_of_frame_pointer = (&arg_arr[i])->offset_of_frame_pointer;
    }

    /* return next node to start traverse on */
    next_node = compound_stmt->left_child;
  } 

  return next_node;
}

/*
 * adds variable declarations to current scope 
 */
void handle_var_decl_line_node(ast_node vdl, symboltable_t * symtab) {

  assert(symtab);
  assert(symtab);

  type_specifier_t this_type = get_datatype(vdl->left_child->left_child);

  ast_node child = vdl->left_child->right_sibling;

  while (child != NULL) {
    char *name = child->left_child->value_string;
    modifier_t mod;
    int byte_size;

    child->left_child->type = this_type;
    if (child->left_child->right_sibling == NULL) {
      mod = SINGLE_DT;
      byte_size = 4;
    } else if (child->left_child->right_sibling->node_type == INT_LITERAL_N) {
      mod = ARRAY_DT;
      byte_size = 4 * child->left_child->right_sibling->value_int;
    } else {
      mod = SINGLE_DT;
      byte_size = 4;
    }
    child->left_child->mod = mod;

    // Insert symnode for variable
    symnode_t *var_node = insert_into_symboltable(symtab, name, child);

    if (var_node == NULL) {
      fprintf(stderr, "error: duplicate variable symbol \'%s\' found. Please fix before continuing.\n", name);
      exit(1);
    }    

    set_node_type(var_node, VAR_SYM);

    // calculate change in stack pointer
    symhashtable_t * symhashtab = vdl->scope_table;

    // Initialize variable
    var_symbol new_var = init_variable(name, this_type, mod, LOCAL_VAR, byte_size);

    // Set variable field in symnode
    set_node_var(var_node, &new_var);

    // Next variable
    child = child->right_sibling;
  }
}

/*
 * adds a scope pointer to leaf symbolhashtable to all children
 * - does not change scope though
 */
void add_scope_to_children(ast_node root, symboltable_t * symtab) {
  assert(root);
  assert(symtab);

  root->scope_table = symtab->leaf;

  for (ast_node child = root->left_child; child != NULL; child = child->right_sibling)
    add_scope_to_children(child, symtab);
  
}

/*
 * Functions for argument structs
 */

/*
 * Inits a new varable struct and returns the structure on stack
 * Note: need to copy returned struct into dynamically allocated memory
 */
var_symbol init_variable(char * name, type_specifier_t type, modifier_t mod, variable_specie_t specie, int byte_size) {

  var_symbol new_var;

  new_var.name = name;     // note, name needs to be saved somewhere else
  new_var.type = type;
  new_var.byte_size = byte_size;
  new_var.modifier = mod;
  new_var.specie = specie;

  return new_var;
}

/* 
 * returns enumerated type_specifier (definied in symtab.h)
 * for an ast_node n input
 */
type_specifier_t get_datatype(ast_node n) {

  assert(n);

  type_specifier_t t;
  switch (n->node_type) {

    case TYPEINT_N:
      t = INT_TS;
      break;

    case VOID_N:
      t = VOID_TS;
      break;

    default:
      t = NULL_TS;  // not a valid type
      break;
  }

  return t;
}

/*
 * Functions for symnodes.
 */

symnode_t * create_symnode(symhashtable_t *hashtable, char *name, ast_node origin) {
  assert(hashtable);
  assert(name);

  symnode_t * node = (symnode_t *)malloc(sizeof(symnode_t));
  assert(node);
  node->name = name;
  node->parent = hashtable;
  node->origin = origin;

  return node;
}
  
void set_node_name(symnode_t *node, char *name) {
  node->name = name;
}

void set_node_type(symnode_t *node, declaration_specifier_t sym_type) {
  assert(node);

  node->sym_type = sym_type;
  //node->s = (symbol *)malloc(sizeof(symbol));
}

void set_node_var(symnode_t *node, var_symbol *var) {
  assert(node);
  assert(var);

  /* node->symbol.variable.[attribute] */
  node->s.v.name = var->name;
  node->s.v.type = var->type;
  node->s.v.modifier = var->modifier;

  //node->s.v.m = SINGLE_DT;
  node->s.v.specie = var->specie;
  node->s.v.byte_size = var->byte_size;
  //node->s.v.offset_of_frame_pointer = var->offset_of_frame_pointer;

}

void set_node_func(symnode_t *node, char * name, type_specifier_t type, int arg_count, var_symbol *arg_arr) {
  assert(node);
  node->name = name;

  /* symbol.function.[attribute] */
  node->s.f.return_type = type;
  node->s.f.arg_count = arg_count;
  node->s.f.arg_arr = arg_arr;

}

int name_is_equal(symnode_t *node, char *name) {

  // 1 is true and 0 is false, but is this the convention in C?
  if (strcmp(node->name, name) == 0) {
    return 1;
  } else {
    return 0;
  }
}

/*
 * Functions for symhashtables.
 */

/* Create an empty symhashtable and return a pointer to it.  The
   parameter entries gives the initial size of the table. */
// symhashtable_t *create_symhashtable(int entries, symtab_type type)
symhashtable_t *create_symhashtable(int entries)    // modified function call
{
  symhashtable_t *hashtable = (symhashtable_t *)calloc(1,sizeof(symhashtable_t));
  assert(hashtable);

  hashtable->size = entries;
  hashtable->table = (symnode_t **)calloc(entries, sizeof(symnode_t *));
  assert(hashtable->table);

  // Initialize stack associated with this hashtable and scope
  hashtable->scopeStack = InitASTStack(INIT_STK_SIZE);
  assert(hashtable->scopeStack);

  // Initialize temporary variable list -- no, just do that for a function
  // hashtable->t_list = init_temp_list();
  // hashtable->local_base_offset = 0;
  // hashtable->local_sp = 0;

  return hashtable;
}

/* Peter Weinberger's hash function, from Aho, Sethi, & Ullman
   p. 436. */
static int hashPJW(char *s, int size) {
  unsigned h = 0, g;
  char *p;

  for (p = s; *p != '\0'; p++) {
      h = (h << 4) + *p;
      if ((g = (h & 0xf0000000)) != 0)
  h ^= (g >> 24) ^ g;
    }

  return h % size;
}

/* Look up an entry in a symhashtable, returning either a pointer to
   the symnode for the entry or NULL.  slot is where to look; if slot
   == NOHASHSLOT, then apply the hash function to figure it out. */
symnode_t *lookup_symhashtable(symhashtable_t *hashtable, char *name,
           int slot) {
  symnode_t *node;       // return NULL if not found

  assert(hashtable);

  if (slot == NOHASHSLOT)
    slot = hashPJW(name, hashtable->size);

  for (node = hashtable->table[slot];
       node != NULL && !name_is_equal(node, name);
       node = node->next)
    ;

  return node;
}

/*
 * returns NULL if not found
 */
symnode_t * look_up_scopes_to_find_symbol(symhashtable_t * first_scope, char *name) {
  if (!first_scope || !name)
    return NULL;

  symnode_t * node = NULL;
  symhashtable_t * cur_scope = first_scope;
  while (cur_scope != NULL && !node) {
    node = lookup_symhashtable(cur_scope, name, NOHASHSLOT);
    cur_scope = cur_scope->parent;
  }

  return node;
}


/* Insert a new entry into a symhashtable, but only if it is not
   already present. */
symnode_t *insert_into_symhashtable(symhashtable_t *hashtable, char *name, ast_node origin) {

  assert(hashtable);

  int slot = hashPJW(name, hashtable->size);
  symnode_t *node = lookup_symhashtable(hashtable, name, NOHASHSLOT);

  /* error check if node already existed! */

  if (node == NULL) {
    node = create_symnode(hashtable, name, origin);
    node->next = hashtable->table[slot];
    hashtable->table[slot] = node;
  } 

  return node;
}


/*
 * Functions for symboltables.
 */

/* Create an empty symbol table. */
symboltable_t  *create_symboltable() {
  symboltable_t *symtab = malloc(sizeof(symboltable_t));
  assert(symtab);

  symhashtable_t *hashtable = create_symhashtable(HASHSIZE);
  hashtable->level = 0;
  hashtable->name = strdup("GLOBAL");

  symtab->root = hashtable;
  symtab->leaf = hashtable;

  return symtab;
}

/* Insert an entry into the innermost scope of symbol table.  First
   make sure it's not already in that scope.  Return a pointer to the
   entry. */
symnode_t *insert_into_symboltable(symboltable_t *symtab, char *name, ast_node origin) {

  assert(symtab);
  assert(symtab->leaf);
  
  symnode_t *node = lookup_symhashtable(symtab->leaf, name, NOHASHSLOT);

  /* error check!! */
  
  if (node == NULL) {
    node = insert_into_symhashtable(symtab->leaf, name, origin);
    return node;
  } else {
    return NULL;
  }  
}

/* Lookup an entry in a symbol table.  If found return a pointer to it.
   Otherwise, return NULL */
symnode_t *lookup_in_symboltable(symboltable_t  *symtab, char *name) {
  symnode_t *node;
  symhashtable_t *hashtable;

  assert(symtab);

  if (symtab->leaf == NULL) {
    printf("leaf is null!\n");
  }

  for (node = NULL, hashtable = symtab->leaf;
       (node == NULL) &&  (hashtable != NULL);
       hashtable = hashtable->parent) {
    node = lookup_symhashtable(hashtable, name, NOHASHSLOT);
  }

  return node;
}

symnode_t * find_in_top_symboltable(symboltable_t * symtab, char * name) {
  if (!name || !symtab)
    return NULL;

  symnode_t * node = NULL;
  node = lookup_symhashtable(symtab->root, name, NOHASHSLOT);


  return node;
}


/*
  Functions for entering and leaving scope
*/

/* 
 * When entering scope, a new symbol table is created at the innermost scope
*/
// void enter_scope(symboltable_t *symtab, int type, ast_node node)
void enter_scope(symboltable_t *symtab, ast_node node, char *name) {
  assert(symtab);

  // Check if current leaf has any children
  if (symtab->leaf->child == NULL) {
    // Child becomes new leaf
    symtab->leaf->child = create_symhashtable(HASHSIZE);
    symtab->leaf->child->level = symtab->leaf->level + 1;
    symtab->leaf->child->sibno = 0;
    symtab->leaf->child->parent = symtab->leaf;

    // pass temp list to child
    symtab->leaf->child->t_list = symtab->leaf->t_list;

    // pass scope owner (function)
    symtab->leaf->child->function_owner = symtab->leaf->function_owner;

    // move leaf to new scope
    symtab->leaf = symtab->leaf->child;

  } else {

    // Current leaf already has a child, new hash table
    // should become sibling of that child
    symhashtable_t *hashtable;

    // Find last right sib of current leaf
    for (hashtable = symtab->leaf->child;
      hashtable->rightsib != NULL; hashtable = hashtable->rightsib);

    hashtable->rightsib = create_symhashtable(HASHSIZE);
    hashtable->rightsib->level = symtab->leaf->level + 1;
    hashtable->rightsib->sibno = hashtable->sibno + 1;
    hashtable->rightsib->parent = symtab->leaf;

    // pass temp list for function
    hashtable->rightsib->t_list = symtab->leaf->t_list;

    // pass scope owner (function)
    hashtable->rightsib->function_owner = symtab->leaf->function_owner;

    // move leaf to new scope
    symtab->leaf = hashtable->rightsib;
  }

  // Label new hash table with node type that begins the new scope
  // i.e. COMPOUND, IF, WHILE, FOR, etc.
  symtab->leaf->name = name;
}

/*
 * When leaving scope, parent hash table becomes new leaf representing
 * current innermost scope
 */
void leave_scope(symboltable_t *symtab) {
  assert(symtab);
  // Destroy stack associated with this scope, don't need it 
  // once done with traversal of scope
  DestroyASTStack(symtab->leaf->scopeStack);
  symtab->leaf->scopeStack = NULL;

  symtab->leaf = symtab->leaf->parent;
}

void print_symhash(symhashtable_t *hashtable) {
  // Print spacing
  for (int i = 0; i < hashtable->level; i++) {
    printf("  ");
  }

  // Print hash table name
  printf("SCOPE: %d-%d %s\n", hashtable->level, hashtable->sibno, hashtable->name);

  // Print hash table contents
  for (int i = 0; i < hashtable->size; i++) {
    symnode_t *node;

    for (node = hashtable->table[i]; node != NULL; node = node->next) {
      // Print spacing
      for (int i = 0; i < hashtable->level + 1; i++) {
        printf("  ");
      }

      printf("NAME: %s, ", node->name);

      switch (node->sym_type) {
        case VAR_SYM:
          printf("VAR_TYPE: %s, ", TYPE_NAME(node->s.v.type));
          printf("MODIFIER: %s, ", MODIFIER_NAME(node->s.v.modifier));
          printf("SIZE: %d, ", node->s.v.byte_size);
          printf("SPECIE: %s ", VAR_SPECIE_NAME(node->s.v.specie));
          printf("[offset %d]", node->s.v.offset_of_frame_pointer);
          break;

        case FUNC_SYM:
          printf("RETURN_TYPE: %s, ", TYPE_NAME(node->s.f.return_type));
          printf("STACK OFFSET: %d, ", node->s.f.stk_offset);
          printf("ARG_COUNT: %d, ", node->s.f.arg_count);
          printf("ARGS: ");
          for (int j = 0; j < node->s.f.arg_count; j++) {
            printf("[%s, %s, %s] ", node->s.f.arg_arr[j].name, TYPE_NAME(node->s.f.arg_arr[j].type), MODIFIER_NAME(node->s.f.arg_arr[j].modifier));
          }

          break;

        default:
          break;
      }

      printf("\n");
    }
  }

  printf("\n");

  // Recurse on each child
  symhashtable_t *table;
  for (table = hashtable->child; table != NULL; table = table->rightsib) {
    print_symhash(table);
  }

}

/*
 * Print contents of symbol table
 */
void print_symtab(symboltable_t *symtab) {
  print_symhash(symtab->root);
}

