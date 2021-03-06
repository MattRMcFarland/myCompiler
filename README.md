# My_Compiler
## Matt McFarland and Yondon Fu - Delights (CS 57 - 16W)
See Yondon's github at [https://github.com/yondonfu]

## General Overview
We've implemented a compiler that can generate Y86 executable for a subset of C that has the same functionality as `gcc`. This subset includes the following features:
* 32 bits integer and integer arrays (pass array by value)
* `for`, `while`, `do-while` loop structures
* Standard boolean operations and conditional testing (`!`,`&&`,`||`,`<`,`<=`,`>`,`>=`,`==`,`!=`)
* `if` and `if else` statements
* pre-increment / decrement as well as post-increment and decrement
* `print` and `read` IO functions 
* `sizeof()`
* `break` and `continue` statements

Note: In order to run the `.ys` files this compiler produces, download the simulator linked in the `simulator_code/` directory and follow the readme instructions there. This is the y86 CPU simulator.

## Files Structure
The file structure our our compiler is as follows:

We added a source directory to manage the increasing number of source files we created:
* `src/ast.h` and `src/ast.c` : AST nodes
* `src/y86_code_gen.h` and `src/y86_code_gen.c` : Quad Translation
* `src/IR_gen.h` and `src/IR_gen.c` : CG (CodeGenerate) functions
* `src/temp_list.h` and `src/temp_list.c` : Temp generation
* `src/check_sym.h` and `src/check_sym.c` : Top-down type-checking
* `src/symtab.h` and `src/symtab.c` : Symboltable functions
* `src/types.h` : Global types and structure file
* `src/toktypes.h` : Token strings
* `src/ast_stack.h` and `src/ast_stack.c` : AST stack (for scope checking)

Top - Level Files
* `build_ys.sh` : Compile and run a `.c` file
* `scan.l` : flex file
* `parser.y` : bison parsing file 
* `Makefile` : create `./gen_target_code` which generates the `.ys` file
* `y86_code_main.c` : source for generation of target code (`.ys` file)

Instructions for compiling y86 code (produce `.ys` file):

`make`

`./generate_target_code <INPUT_FILE> <OUTPUT_NAME_PREFIX>`

Instructions for running tests:

`./build_ys.sh tests/<input_file_name> <output_file_name> [Optional: -g]`

## Implementation Specifics

For the final submission of the compiler, we simply ironed out the bugs from the last milestone of the project (generation of target code). Major refactors are listed here:
* Changing the way target code is printed. Before, we returned strings from functions that would identify an operands source and destination. Passing around static strings was an issue, so we pushed the printing of the target code to `get_source_value()` and `get_dest_value()`. This simplified the quad translation by a lot. 
* Altering Prolog and Post Return. We realized that we don't actually keep track of how many things are pushed onto the stack for a function call because we don't need to save and restore registers between function calls (because values never live in registers between quads). So when a function returns, we manually reset the stack pointer to where it ought live just below the temps and locals for the caller. Before we implemented this reset, we were encountering a lot of off-by-one stack returns. 
* Saving the return value in a temp. In the Post Return quad, we now save the returned value in %eax register to a temp. Without saving %eax to a temp in memory, we were clobbering the return value by expecting to use the return value from two function calls as operands in an expression. Such as : `int a = my_func(1) + my_func(2)`.

## Extra Features

### sizeof()

To implement the sizeof operator we added a `byte_size` field to the `var_symbol` structs contained in our `symnode` structs. Whenever we add a `symnode` to our symbol table, we call `handle_func_decl_node`, which might handle function argument variables, or `handle_var_decl_node`, which might handle variable declarations. In each function, if we find an integer variable, we set the `byte_size` to 4 and if we find an array variable, we set the `byte_size` to 4 * length of the array.

In `y86_code_gen.c`, we handle SIZEOF_Q quads by first checking if the argument for the operator is an array, an element in the array or just an integer variable. If the argument is an array or an integer variable, we just move the value of the `byte_size` field into the temp that is returned by the sizeof operation. If the argument is an element in an array, we grab the corresponding size for the type of the array element (will be 4 for an integer) and move that value into the temp returned by the sizeof operation.

### Break and Continue

To implement break and continue statements, we needed a way to jump to parent control structures from a particular break or continue node, so we added pointers from all AST nodes to their parents. We use the function `post_process_ast` in `ast.c` to recursively add parent pointers for all nodes in an AST after first generating the AST using our parser.

We also define a `lookup_parent_block` function in `ast.c` to find the AST node that represents the parent control structure for a given break or continue AST node. The function follows parent pointers up from an AST node until it reaches a parent that represents a control structure and then returns that node. If there are no parents that represent control structures, the function returns NULL.

We handle quad generation for break and continue AST nodes in `IR_gen.c`. For a break node, we find the parent control structure using `lookup_parent_block` and switch on the type of the control structure. We create an exit label based on the control structure's node ID and the type of control structure (ex. for a while loop we create a label of the format ID_WHILE_EXIT). We then generate a GOTO_Q quad that jumps to this exit label. For a continue node, we find the parent control structure using `lookup_parent_block` and switch on the type of the control structure. We create a test label (indicates the testing portion of a loop) based on the control structure's node ID and the type of control structure (ex. for a while loop we create a label of the format ID_WHILE_TEST). Note that the label for a for loop is slightly different because we need to make sure to update the for loop variable before jumping to the test portion of the loop. Consequently, we instead create a label that designates the update portion of the label of the form ID_FOR_UPDATE. We then generate a GOTO_Q quad that jumps to this label.

### Post Increment and Post Decrement

Originally our compiler only supported pre increment and pre decrement operations. In this final submission we also implemented post increment and post decrement operations. Increment and decrement operations work similarly, one increases a variable by 1 while the other decreases a variable by 1, so for the purposes of explaining pre operations and post operations we will just talk about increment.

Both pre increment and post increment increase the value of a variable. The main difference between the two operations is in the value they return. Pre increment returns the increased value of the variable. Post increment returns the original value of the variable before it is increased.

In our implementation we separate our original INC_Q quads into a PRE_INC_Q quad and a POST_INC_Q quad. In `y86_code_gen.c`, when we find a PRE_INC_Q quad we update the variable using an addition operation and also move the updated value into a temp that is returned by the operation. When we find a POST_INC_Q quad, we first move the original value of the variable into a temp that is returned by the operation and then we update the variable using an addition operation.

## Testing Files
All test files live in the `tests/` directory. There are three new subdirectories have been added there:
* `edge_cases/`: Includes the stress-tests provided by instructors
* `extra_features/`: Includes test files that demonstrate our extra features
* `my_stress_tests/`: Includes some functions and files that push the compiler

Important test files are highlighted below. Most of the files in `tests/` are rudimentary tests.

### `edge_cases/error.c`
This file highlighted a contested "dark corner" of our formal grammar. Although `gcc` does not compile this file because it prevents the assignment to expressions, our compiler does not throw an error for this file. Our compiler interprets `x + y = 2 + 3` as `x + (y = 2 + 3)` (which `gcc` can compile). We decided not to alter the grammar that was given to us and to default to the latter interpretation of expressions. The `%right` associativity given to the `=` token means that our parser will default to the latter understanding of assignments in the context of other operations. The only way to prevent this without altering the grammar would be to ban the use of assignments as operands to other expressions, which we believe would result in a "less correct" compiler than the difference between `x + y = 2 + 3` as `x + (y = 2 + 3)`.

### `extra_features/inc_dec.c`
This file illustrates how the post increment and post decrement operations work. Note: We encountered an interesting issue here when trying to doubly increment or decrement (`((a)++)++`). This fails our test file, but it also fails in `gcc` because you cannot increment the value of an expression. Only variables can be incremented / decremented because the value must be stored somewhere and gcc doesn't allow for the assignment to an expression.

### `extra_features/sizeof.c`
Shows all the use cases of sizeof. Highlights include:
* `sizeof(local_var)` returns the size of that variable (works for global ints too).
* `sizeof(int param)` returns the size of an int parameter.
* `sizeof(local_arr)` returns the total size of the array (works for global arrays too).
* `sizeof(local_arr[index])` returns the size of an element in the array (works for global arrays too).
* `sizeof(param_arr[index])` returns the size of an element in the parameter array.
* `sizeof(int param_arr[])` produces a syntax error -- the size of the array passed to the function is unknown until runtime, and thus sizeof could only return the size of the array handle. Since that is essentially a pointer (something we don't support), we decided to disallow that request since pointers aren't included in our support set of C.

### `extra_features/tbreakcontinue.c` 
Testing the functionality of break and continue in various for loops and while statements. 

### `my_stress_tests/factorial.c`
Factorial implementation with some logic. This can compute the factorial of the numbers 0 - 13 (too larger means overflow).

### `my_stress_tests/recurse.c`
Simple recursion fuction. Prints decrement input until zero.

### `my_stress_tests/sort.c`
This is an implementation of the selection sort algorithm with swap-in-place and pass array by value to the sorting function (since the sort function can manipulate values in the array without copying them). There's a lot of looping, conditional testing and array manipulation that occurs here.

### `my_stress_tests/test_simple.c` 
This test stretches the parsing, type checking and AST tree creation. This file makes A LOT of temps.

### `tops.c`
Exhaustively tests all operatons and expected outputs.

### `tcond.c`
This files contains many loop iteration tests and conditional tests. It's also fun to watch it run.

### `array.c`
This file tests how local, global and parameter arrays are handled by manipulating values and then printing them to the terminal. We used this file to iron out issues with passing arrays as paramters as well smooth out prologs and epilogs.

### `tnested.c`
Tests scopes and variable shadowing.

### `nestarray.c`
Originally, we had trouble evaluating arrays indexed by other arrays. This file demonstrates how we now evaluate indices and array values from the interior to the outermost array.

### `tfunc.c`
Tests the different ways that function can be declared and called (void parameters, void returns, called without args).

### `nomain.c`
Shows how our compiler will report an error if no `main` function is declared.






