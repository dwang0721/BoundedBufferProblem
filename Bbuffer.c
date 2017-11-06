#define _BSD_SOURCE
#include<stdio.h>
#include<signal.h>
#include<unistd.h>
#include<stdlib.h>
#include<pthread.h>
#include<semaphore.h>
#include<assert.h>
#define MAXBUFFER 100000000
#define BUFFER_SIZE 10

/* struct for buffer*/
typedef struct
{
	int queue[BUFFER_SIZE + 1];
	int head;
	int tail;
} buffer;

//mutex for input bufifer and output buffer
pthread_mutex_t mutex_drop,mutex_tool,mutex_ibuffer,mutex_obuffer;
pthread_cond_t cond_gen, cond_op, cond_tool;
sem_t empty, full;

//input and output buffer
buffer *ibuffer;
buffer *obuffer;

// number of operator, number of waiting operator, number of product dropped
int num_op,num_wait_op,num_drop;

int num_material[4]; // record the # of each type materials on ibuffer, [0] unused, [1]->material 1, [2]->material 2, [3]->material 3
int num_product[4]; // record the # of each type product on num_product, [0] unused, [1]->product 1, [2]->product 2, [3]->product 3
int product_flag[4]; // whether this product can be produced this time, [0] unused, [1]->product 1, [2]->product 2, [3]->product 3

int recent_material_produced[3]; // record the most recent 3 materials produced by the generator
int recent_material_taken[4]; // record the # of each material taken by the operator, [0] unused, [1]->material 1, [2]->material 2, [3]->material 3

int toolbox [4]; // tools 1,2,3 availability. [0] unused, [1]->tool 1, [2]->tool 2, [3]->tool 3
int sig_pause; // variables to handle pause


/*check if it is okay to generat recent materail*/
int safe_to_gen(int gennow)
{
	if(gennow==recent_material_produced[0]) return 0; //avoid two identical items
	if(gennow==recent_material_produced[1] && recent_material_produced[0]==recent_material_produced[2]) return 0; // avoid repeating pattern

	// check if the rencent material generate too fast
	int count=0;
	for (int i = 1 ; i < 4; i++) {
		if (recent_material_taken[gennow]-recent_material_taken[i]>5) {
				count++;
		}
	}
	if (count >= 2 && (recent_material_produced[1]== gennow || recent_material_produced[2]==gennow)) return 0;
	return 1;
}


/*check input buffer size*/
int size_q(buffer *q)
{
	if(q->head==q->tail) return 0; //queue empty
	if(q->head-q->tail==BUFFER_SIZE||q->tail-q->head==1) return -1;//full,return -1
	if (q->head-q->tail>0) //return number of materials
		return q->head-q->tail;
	else
		return BUFFER_SIZE + 1 +q->head-q->tail;
}

/*put material into input buffer*/
void push_material(buffer *q,int x) // input buffer and material
{
	q->queue[q->head]=x;
	q->head++;
	recent_material_produced[2]=recent_material_produced[1];
	recent_material_produced[1]=recent_material_produced[0];
	recent_material_produced[0]=x;
	if(q->head== BUFFER_SIZE + 1) q->head=0;
	return;
}

/* push the the product out of buffer*/
void push_product(buffer *q,int x) // output buffer and product
{
	if(q->head-q->tail==BUFFER_SIZE) q->tail=1;
	if(q->tail-q->head==1) {q->tail++; if(q->tail==BUFFER_SIZE + 1) q->tail=0;}
	q->queue[q->head]=x;
	q->head++;
	if(q->head==BUFFER_SIZE + 1) q->head=0;

	return;
}

/*get material from input buffer*/
int get_material(buffer *q)
{
	int i;
	i=q->queue[q->tail];
	q->tail++;
	if(q->tail==BUFFER_SIZE + 1) q->tail=0;
	return i;
}

int safe_to_produce(int x,int y)
{
	// 1,2->1; 1,3->2; 2,3->3
	return product_flag [x+y-2];
}

/*produce this product*/
void product_out(int x,int y)
{
	product_flag[1]=1;
	product_flag[2]=1;
	product_flag[3]=1;

	// material 1 and 2, ->  product 1
	// material 3 and 1, ->  product 2
	// material 3 and 2, ->  product 3
	int product_number = x+y-2;
	push_product(obuffer, product_number); // put product on obuffer
	num_product[product_number]++; // add 1 to the num_product
	product_flag[product_number]= 0; // can not produce the product by the next operator

	//product number difference should be less than 10
	for (int i=1; i<4; i++)
	{
		for (int j=i+1; j<4; j++)
		{
			if (num_product[i]-num_product[j]>=9)
			{
				product_flag[i] = 0;
			}else if(num_product[j]-num_product[i]>=9)
			{
				product_flag[j] = 0;
			}
		}
	}
}

void print_buffer(buffer *q){
	int i=q->head;
	while (i!=q->tail)
	{
		i--;
		if(i==-1) i=10;
		printf("%d ",q->queue[i]);
	}
}


/*generator thread*/
void* generator(void *ptr){

	int genNum = *(int*)ptr;

	while (1) {
		sem_wait(&empty);
		pthread_mutex_lock(&mutex_ibuffer);

			// generator wait if it can not generate material now
			while(!safe_to_gen(genNum)) {
				pthread_cond_signal(&cond_gen);// prevent deadlock
				pthread_cond_wait(&cond_gen,&mutex_ibuffer); //wait
			}

				push_material(ibuffer,genNum);
				num_material[genNum]++;
				recent_material_taken[genNum]++;

		pthread_cond_signal(&cond_gen);
		pthread_mutex_unlock(&mutex_ibuffer);
		sem_post(&full);

		while(sig_pause) usleep(500);
	}

	pthread_exit(0);
}


/*operator thread*/
void* operator(void *ptr)
{
	int mat_x,mat_y,output_flag;
	int toolx, tooly;
	while(1)
	{
		output_flag=0;

		// take the first material
		sem_wait(&full);
		pthread_mutex_lock(&mutex_ibuffer);
		mat_x=get_material(ibuffer);
		pthread_mutex_unlock(&mutex_ibuffer);
		sem_post(&empty);


		// take the second material
		sem_wait(&full);
		pthread_mutex_lock(&mutex_ibuffer);
		mat_y=get_material(ibuffer);
		pthread_mutex_unlock(&mutex_ibuffer);
		sem_post(&empty);


		// handle same materials situation
		while (mat_x==mat_y)
		{
			sem_wait(&full);
			pthread_mutex_lock(&mutex_ibuffer);

					mat_y=get_material(ibuffer); // take the next matrial
					push_material(ibuffer,mat_x); // put back one

			pthread_mutex_unlock(&mutex_ibuffer);
			sem_post(&empty);
		}

		// operator has no tools
		toolx=0;
		tooly=0;

		pthread_mutex_lock(&mutex_tool);
		// loop if toolx and tooly are not aquired
		while ((toolx + tooly)!=2){

			//drop tool x
			if (toolx == 1) {
				toolbox[mat_x] ++;
				toolx -- ;
				pthread_cond_signal(&cond_tool);
			}

			// drop tool y
			if (tooly == 1) {
				toolbox[mat_y] ++;
				tooly -- ;
				pthread_cond_signal(&cond_tool);
			}

			// wait on tool x
			while (toolbox[mat_x] == 0) {
				pthread_cond_wait (&cond_tool,&mutex_tool); // wait
			}

			// wait complete and take tool x
			toolbox[mat_x] --;
			toolx ++ ;

			//try take tool y
			while (toolbox[mat_y]== 0) { // if other is taking tool y
				if (toolx == 1) { //if operator is holding toolx.
					toolbox[mat_x] ++;
					toolx -- ; // put back tool x
					pthread_cond_signal(&cond_tool);
				}
				pthread_cond_wait (&cond_tool,&mutex_tool); // wait on tools
				continue;
			}

			// take tool y
			toolbox[mat_y] --;
			tooly ++;

		}

		pthread_mutex_unlock(&mutex_tool);

		usleep(1000*(10+rand()%990));// need 0.01s to 1s processing time

		// return the tools.
		pthread_mutex_lock(&mutex_tool);
		toolbox[mat_x]=1;
		toolbox[mat_y]=1;
		pthread_cond_signal (&cond_tool);
		pthread_mutex_unlock(&mutex_tool);

		pthread_mutex_lock(&mutex_obuffer);
		//wait till the product can be put into the output queue
		if(!safe_to_produce(mat_x,mat_y)) {
			pthread_mutex_lock(&mutex_drop);
			num_wait_op++;
			//handeling deadlock
			if(num_wait_op==num_op)
			{
				num_drop++;

				pthread_mutex_lock(&mutex_ibuffer);
				//drop the product
				recent_material_taken[mat_x]--;
				recent_material_taken[mat_y]--;
				pthread_mutex_unlock(&mutex_ibuffer);
				pthread_mutex_unlock(&mutex_obuffer);

				num_wait_op--;
				pthread_mutex_unlock(&mutex_drop);
				continue;
			}
			pthread_mutex_unlock(&mutex_drop);
			output_flag=1;
		}

		// wait if it can not output to out buffer
		while(!safe_to_produce(mat_x,mat_y))	pthread_cond_wait(&cond_op, &mutex_obuffer);

		product_out(mat_x,mat_y); //output product to buffer
		pthread_cond_signal(&cond_op);
		pthread_mutex_unlock(&mutex_obuffer);


		pthread_mutex_lock(&mutex_drop);
		if(output_flag) num_wait_op--;/*decrease wait number*/
		pthread_mutex_unlock(&mutex_drop);

		while(sig_pause) usleep(500);

	}
	pthread_exit(0);
}



/*dynamic output*/
void* dynamic_output(void* ptr)
{
	int i, time_count =0;
	while(1){

	while(sig_pause) usleep(500);

	pthread_mutex_lock(&mutex_ibuffer); /*get buffer*/
	printf("\033[2J\033[1;1H");

	printf("Status Summary\n");
	printf("---------------------------------------------------\n");
	printf("Generator produced->| M1: %4d| M2: %4d| M3: %4d|\n",num_material[1],num_material[2],num_material[3]);
	printf("Operator  produced->| P1: %4d| P2: %4d| P3: %4d|\n",num_product[1],num_product[2],num_product[3]);
	printf("---------------------------------------------------\n\n");

	i=size_q(ibuffer);
	if(i==-1) i=10;
	printf("Input    Size: %2d \n",i);
	printf("Input  Status: ");


	//input status
 	print_buffer(ibuffer);
	pthread_mutex_unlock(&mutex_ibuffer);
	printf("\n");

	// output tool status
	pthread_mutex_lock(&mutex_tool);
	printf("Tools  Status: | X: %d| Y: %d| Z: %d|\n", toolbox[1],toolbox[2],toolbox[3] );
	pthread_mutex_unlock(&mutex_tool);

	//output status
	pthread_mutex_lock(&mutex_obuffer);
	printf("Output Status: ");
	print_buffer(obuffer);
	if (size_q(obuffer)==-1) printf("...");
	pthread_mutex_unlock(&mutex_obuffer);


	printf("\n");

	// arrow animation
	time_count %= 9;
	printf("               ");
	for (int j=0; j< time_count; j++) printf("  ");
	printf("-->\n");
	time_count ++;
	printf("\n");


	printf("%d of %d Operators are waiting.\n", num_wait_op, num_op);
	printf("When all operators are waiting, drop a product.\n");
	printf("%d of products have been dropped (deadlock)\n", num_drop);

	printf("\nCTRL+C : Process Termination\n");
	printf("P->Enter: Pause\n");

	if(num_product[1]+num_product[2]+num_product[3]>MAXBUFFER)
	{
		printf("MAX output reached. (larger than %d)\n",MAXBUFFER);
		exit(0);
	}
	sleep(1);
	}
	pthread_exit(0);
}

/*pause thread*/
void* pause_thread(void* ptr) {
		while (1){
			char key = getchar();
			if (key == 'p'|| key=='P') {
				sig_pause = 1;
				printf("\n\n\n");
				printf("Program paused.\n R->Enter: Resume\n Ctrl+C: To Terminate\n");
			} else if (key == 'r'|| key=='R'){
				sig_pause = 0;
			}
		}
	exit(0);
}

/*quit signal handler*/
void quit_handler(int sig)
{
	if(sig==SIGINT)
	{
			printf("\033[2J\033[1;1H");
			printf("Process Terminated\n");
			exit(0);
	}
}


/*main function*/

int main(void)
{
	//generator, operator, display, pause thread
	pthread_t gen[4], op[11],display, pauseID;


	int genNums[4] = {0,1,2,3}; // generator number [0] not used, [1-3] generator 1-3

	printf("\033[2J\033[1;1H");


	// prompt for user input
	printf("   * *Operator and 3 Generators* *\n");
	printf("   *░░░░░░░░░░░░░░░░░░░░░░░░░░░░░*\n");
	printf("   *░░░░░░░░░░░░░▄▄▄▄▄▄▄░░░░░░░░░*\n");
	printf("   *░░░░░░░░░▄▀▀▀░░░░░░░▀▄░░░░░░░*\n");
	printf("   *░░░░░░░▄▀░░░░░░░░░░░░▀▄░░░░░░*\n");
	printf("   *░░░░░░▄▀░░░░░░░░░░▄▀▀▄▀▄░░░░░*\n");
	printf("   *░░░░▄▀░░░░░░░░░░▄▀░░██▄▀▄░░░░*\n");
	printf("   *░░░▄▀░░▄▀▀▀▄░░░░█░░░▀▀░█▀▄░░░*\n");
	printf("   *░░░█░░█▄▄░░░█░░░▀▄░░░░░▐░█░░░*\n");
	printf("   *░░▐▌░░█▀▀░░▄▀░░░░░▀▄▄▄▄▀░░█░░*\n");
	printf("   *░░▐▌░░█░░░▄▀░░░░░░░░░░░░░░█░░*\n");
	printf("   *░░▐▌░░░░░░░░░░░░░░░▄░░░░░░▐▌░*\n");
	printf("   *░░▐▌░░░░░░░░░▄░░░░░█░░░░░░▐▌░*\n");
	printf("   *░░░█░░░░░░░░░▀█▄░░▄█░░░░░░▐▌░*\n");
	printf("   *░░░▐▌░░░░░░░░░░▀▀▀▀░░░░░░░▐▌░*\n");
	printf("   *░░░░█░░░░░░░░░░░░░░░░░░░░░█░░*\n");
	printf("   *░░░░▐▌▀▄░░░░░░░░░░░░░░░░░▐▌░░*\n");
	printf("   *░░░░░█░░▀░░░░░░░░░░░░░░░░▀░░░*\n");
	printf("   *░░░░░░░░░░░░░░░░░░░░░░░░░░░░░*\n");
	printf("   * * * * * * * * * * * * * * * *\n");
	printf("* Choose how many operators do you need? (1-10)\n");
	printf("* Invalid input generates defualt three operators\n");
	printf(">>");


	num_op=3;
	num_wait_op=0;
	num_drop=0;

	//get user input
	int input;
	scanf("%d", &input);
	if (input >=1 && input<= 10) {
		num_op = input;
	}else{
		num_op = 3 ;
	}

	//mutex and conditional variable initiation
	pthread_mutex_init(&mutex_ibuffer,NULL);
	pthread_mutex_init(&mutex_obuffer,NULL);
	pthread_mutex_init(&mutex_drop,NULL);
	pthread_mutex_init(&mutex_tool,NULL);
	pthread_cond_init(&cond_gen,NULL);
	pthread_cond_init(&cond_op,NULL);
	pthread_cond_init(&cond_tool,NULL);

	//semaphore
	sem_init(&full, 0, 0);
	sem_init(&empty, 0, BUFFER_SIZE);

	recent_material_taken[1]=0;
	recent_material_taken[2]=0;
	recent_material_taken[3]=0;
	recent_material_produced[0]=0;
	recent_material_produced[1]=0;
	recent_material_produced[2]=0;

	// malloc and initiation for input and output buffer
	obuffer = (buffer *)malloc(sizeof(buffer));
	obuffer->tail=0;
	obuffer ->head=0;
	ibuffer = (buffer *)malloc(sizeof(buffer));
	ibuffer->tail=0;
	ibuffer->head=0;

	// initiate
	num_material[1]=0;
	num_material[2]=0;
	num_material[3]=0;
	num_product[1]=0;
	num_product[2]=0;
	num_product[3]=0;
	product_flag[1]=1;
	product_flag[2]=1;
	product_flag[3]=1;
	toolbox [1] = 1;
	toolbox [2] = 1;
	toolbox [3] = 1;
	sig_pause = 0;

	// handle ctrl+c interupt
	signal(SIGINT, quit_handler);

	// create thread
	pthread_create(&display,NULL,dynamic_output,NULL);
	//generator thread
	for (int i=1;i<=3;i++) {
		pthread_create(&gen[i],NULL,generator, &genNums[i]);
		}
	usleep(50000); // let generator work for a while
	//operator thread
	for (int i=1;i<=num_op;i++) {
		pthread_create(&op[i],NULL,operator,NULL);
		}
	//pause thread
	pthread_create(&pauseID,NULL,pause_thread,NULL);



	// wait for display thraed
	pthread_join(display,NULL);
	// wait for generator thread
	for (int i=1;i<=3;i++) {
		pthread_join(gen[i],NULL);
	}
	//wait for operator thread
	for (int i=1;i<=num_op;i++) {
		pthread_join(op[i],NULL);
	}
	//wait for pause thread
	pthread_join(pauseID, NULL);

	//destroy all mutex and conditional variables
	pthread_mutex_destroy(&mutex_ibuffer);
	pthread_mutex_destroy(&mutex_obuffer);
	pthread_mutex_destroy(&mutex_tool);
	pthread_mutex_destroy(&mutex_drop);
	pthread_cond_destroy(&cond_gen);
	pthread_cond_destroy(&cond_op);
	pthread_cond_destroy(&cond_tool);

	free(ibuffer);
	free(obuffer);

}
