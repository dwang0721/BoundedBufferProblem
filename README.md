### PROBLEM STATEMENT:

There are 3 generators, each of which produce unique material. All three materials are stored in an input buffer of size 10 before being forwarded to 3 operators. Each operator are assigned the same priorities, and are responsible for producing products utilizing 2 of 3 of the provided materials. 3 tools are provided to operator, of which 2 will be used to create one product at a time. When an operator is supplied the required materials and tools, a product can be produced between 0.01 and 1 second.  

Materials and tools can be assigned separately at different times while an operator is waiting. If an operator decides to make another product before completing the current product, the current materials and tools must be unassigned to allow for use by other operators.

All products are placed into a unlimited-sized output queue. An operator cannot start a new product before placing the current product into the output queue. Two restrictions may apply to these products: 

    1) No two similiar products can be placed consecutively in the same queue. Two products are defined to be similar if both are created from the same materials. 
    2) The difference of the number of any two kinds of procuded products must be less than 10. For example, 10 units of product A and 15 units of product B is legal, but 10 units of product A and 21 units of product B is illegal because there is a difference of 11, which is greater than 10.
    

Product 1: Made by materials  1 & 2,  and tools x & y

Product 2: Made by materials  2 & 3,  and tools y & z

Product 3: Made by materials  3 & 1,  and tools z & x

This program provided "pause" and "resume" options so that the user can pause the execution as many times as needed and resume it again. letter "p" to pause the execution, letter "r" to resume the execution. 




## Documentation

### Global Variables : 
These following global variables are used to keep track of materials, products and tools. 

### Input and output buffer data struct:
Both input and output buffer use a circular queue data structure: 

typedef struct
{
    int queue[];
    int head;
    int tail;
} buffer;

#### toolbox [4]

tools 1,2,3 availability. [0] unused, [1]->tool 1, [2]->tool 2, [3]->tool 3

#### Num_material[4], num_product[4];		

record the # of each type materials/products on the buffer, [0] unused, [1]->material 1, [2]->material 2, [3]->material 3


### Critical Section Variables: 

Mutex, Semaphores and Conditional variables are used to protect these global variables from race conditions. 

#### Mutext :

Wait_mutex,	 	protect variable wait_op
Tool_mutex, 		protect tool_box
Input_mutex,	 	protect input buffer
output_mutex , 	protect output buffer

#### Semarphore:

Empty	:  initiated as full buffer size
Full 	:  initiated as full buffer 0

#### Conditional Wait:		

cond_gen, 		a generator must wait if the material potentially causes deadlock
cond_op, 		an operator must wait if the product potentially causes deadlock 
cond_tool;		an operator must wait for the tools if the tool is not available





### Additional Variables:
Additional variables are used coordinate producers and consumers so as to prevent potential deadlock.

#### safe _to_produce[4]

record the # of each type product on obuffer, [0] unused, [1]->product 1, [2]->product 2, [3]->product 3

#### recent_material_produced[3]

record the most recent 3 materials produced by the generator

#### recent_material_taken[4]

record the # of each material taken by the operator, [0] unused, [1]->material 1, [2]->material 2, [3]->material 3

### Functions:

Generator and Operator threads:

#### void* generator(void *ptr)

A generator thread keeps checking if he can produce the material by calling safe_to_gen(), if it returns false, it should wait on cond_gen. If it is good to go, push the material onto the input buffer, increase the material number.

#### void* operator(void *ptr)

An operator will take the first material, and second material. If the materials are the same, it will keep swapping one material with the last material in the input buffer, until he gets different materials.He will try to take the tools based on the material taken. If an operator has acquired one tool and waiting for another, he puts the tool back and start over again to avoid deadlock. Before he puts the generated product to the output buffer, he checks the output buffer. The it is the same as the previous product, he should wait. If all operator wait, drop one product to avoid the deadlock. 

### Safety check:

#### safe_to_gen(int gennow)

This function is used by generator to check if it safe to generate a material. There are 3 situations that is not safe to produce an material: 2 adjacent identical items; 2 adjacent repeating pattern such as 1212; too many of this material. 

#### int safe_to_produce(int x,int y)

Check if the operator is safe to produce a product. 

### Deadlock Prevention and Treatment 

In general, the deadlock is avoided by the above functions. There are situations that deadlocks that is unavoidable: all operators are waiting to put the product on the output buffer, because they all holding the same product and the last element of the buffer are the same. One of the operator needs to drop an product and create a different product. But the situation is rare. 

For more details please read the documentation inside the script.

