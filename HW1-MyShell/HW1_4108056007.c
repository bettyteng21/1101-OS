#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <string.h>
#include <dirent.h>
#include <wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <termios.h>
#include <stdbool.h>
#include <fcntl.h>

#define MAX_BUFFER 200
#define STACK_SIZE 1000
#define BEGIN(x, y) "\033["#x";"#y"m" //x: string background, y: string font color (for prompt)
#define CLOSE "\033[0m"

char *buff;
char history[STACK_SIZE][MAX_BUFFER];
int stack_top=-1, curr_index=0; //curr_index: saves which index of history is currently viewing
bool background=false, redirect=false, append=false;

void push(char* new_command){
	if(stack_top==STACK_SIZE-1) printf("Stack is full!\n");
	else{
		int i;
		stack_top++;
		for (i=0; i<MAX_BUFFER && new_command[i]!='\0'; i++){
			history[stack_top][i]=new_command[i];
		}
		curr_index=stack_top+1;
	}
}

void display_up(){	
	curr_index--;
	if(stack_top==-1 || curr_index==-1) printf("No previous command\n");
	printf("%s\n", history[curr_index]);
}

void display_down(){
	curr_index++;
	if(stack_top==-1 || curr_index==-1) printf("No previous command\n");
	printf("%s\n", history[curr_index]);	
}

static struct termios old, current;
void initTermios(int echo) {
	tcgetattr(0, &old); /* grab old terminal i/o settings */
	current = old; /* make new settings same as old settings */
	current.c_lflag &= ~ICANON; /* disable buffered i/o */
	if (echo) {
		current.c_lflag |= ECHO; /* set echo mode */
	} else {
		current.c_lflag &= ~ECHO; /* set no echo mode */
	}
	tcsetattr(0, TCSANOW, &current); /* use these new terminal i/o settings now */
}

/* Restore old terminal i/o settings */
void resetTermios(void) {
	tcsetattr(0, TCSANOW, &old);
}

/* Read 1 character - echo defines echo mode */
char getch_(int echo) {
	char ch;
	initTermios(echo);
	ch = getchar();
	resetTermios();
	return ch;
}

/* Read 1 character without echo */
char getch(void) {
	return getch_(0);
}

/* Read 1 character with echo */
char getche(void) {
	return getch_(1);
}

void ECHO_COMMAND(int flag, int outfd, int outfd_back){
	int i; 
	/* flag==0, command type: echo "Hello" */
	if(flag==0){ 
		if(redirect==true){
			dup2(outfd, 1); //redirect output to file par3

			for (i=6; i<MAX_BUFFER && buff[i]!='"'; i++){  //don't print the "echo" and the double quotes
				printf("%c",buff[i]);
			}
			printf("\n");

			fflush(stdout);
			dup2(outfd_back, 1);
			close(outfd);
		}
		else{
			for (i=6; i<MAX_BUFFER && buff[i]!='"'; i++){  //don't print the "echo" and the double quotes
				printf("%c",buff[i]);
			}
			printf("\n");
		}
	}
	/* flag==1, command type: echo $PATH */
	else if(flag==1){  
		char dir[MAX_BUFFER]="\0";
		for (i=6; i<MAX_BUFFER && buff[i]!='\0' && buff[i]!=' '; i++){  //don't read the "echo" and the '$'
			dir[i-6]=buff[i];			
		}
		dir[i-6]='\0'; 

		char *temp=getenv(dir);
		if(redirect==true){
			dup2(outfd, 1); //redirect output to file par3

			if (temp!=NULL){
				printf("%s\n", temp);
			}
			else printf("bash: echo: %s: No such environment variable\n", dir);

			fflush(stdout);
			dup2(outfd_back, 1);
			close(outfd);
		}
		else{
			if (temp!=NULL){
				printf("%s\n", temp);
			}
			else printf("bash: echo: %s: No such environment variable\n", dir);
		}		
	}
	/* flag==2, command type: echo Hello */
	else if(flag==2){  
		if(redirect==true){
			dup2(outfd, 1); //redirect output to file par3

			for (i=5; i<MAX_BUFFER && buff[i]!='\0' && buff[i]!='>'; i++){  //don't print the "echo" 
				printf("%c",buff[i]);
			}
			printf("\n");

			fflush(stdout);
			dup2(outfd_back, 1);
			close(outfd);
		}
		else{
			for (i=5; i<MAX_BUFFER && buff[i]!='\0'; i++){  //don't print the "echo" 
				printf("%c",buff[i]);
			}
			printf("\n");
		}
	}
}

void CD(int flag){
	int i;
	/* flag==0, command type: cd */
	if (flag==0){  
		chdir(getenv("HOME"));
	}
	/* flag==1, command type: cd /home */
	else if(flag==1){  
		char dir[MAX_BUFFER]="\0";
		for (i=3; i<MAX_BUFFER && buff[i]!='\0'; i++){  //Note: last char in fgets is '\n'
			dir[i-3]=buff[i];
		}
		dir[i-3]='\0';

		int temp= chdir(dir);
		if(temp<0) printf("bash: cd: %s: No such file or directory\n", dir);		
	}
}


void EXPORT(void ){
	char name[100], value[MAX_BUFFER];
	int i, diff=0; 
	for (i=7; i<MAX_BUFFER && buff[i]!='='; i++){ //find environment variable name
		name[i-7]=buff[i];
	}
	name[i-7]='\0'; i++;
	
	for (; i<MAX_BUFFER && buff[i]!='\0'; i++){ //find environment variable value
		value[diff]=buff[i];
		diff++;
	}
	value[diff]='\0';

	setenv(name, value, 1); //set environment variable
}

void SLEEP(void ){
	char temp_time[5];
	int time, i;
	for(i=6; i<MAX_BUFFER && buff[i]!='\0'; i++){
		temp_time[i-6]=buff[i];
	}
	temp_time[i-6]='\0';
	time= atoi(temp_time);

	sleep(time); 
}


int main(void){	
	int i, echo_flag=-1, cd_flag=-1, index=0, buff_size=0, process_num=0, child_pid;
	char curr_path[MAX_BUFFER]="", arrow, prompt[100];
	char *ps, *temp_input;
	bool bg_finish=true, pre_is_arrow=false;
	buff=(char*)malloc(MAX_BUFFER*sizeof(char));	

	while(1){
		pid_t pid; buff_size=0; redirect=false; append=false;
		
		/////////////////////////prompt/////////////////////////
		getcwd(curr_path, sizeof(curr_path));
		printf(BEGIN(49, 32)"%s$ "CLOSE, curr_path);	
		
		/////////////////////////get the first character/////////////////////////
		buff=(char*)malloc(MAX_BUFFER*sizeof(char));
		buff[0]=getch();  //catch the char with out printing on screen
		if(buff[0]=='\033'){ //arrow key pressed
			getch();
			switch(getch()){
				case 'A': //up arrow
					display_up();
					pre_is_arrow=true;
					continue; 
					break;
				case 'B': //down arrow
					display_down();
					pre_is_arrow=true;
					continue;
					break;
			}
		}
		else if(buff[0]==10){ //only press enter key: execute the command after select from up/down key
			if(pre_is_arrow==true) {
				strcpy(buff, history[curr_index]);
				
			}
			else {
				printf("\n");
				continue;  //if previous command is not from arrow, do nothing 
			}
		}
		else{  //normal command input(not enter nor arrow)
			temp_input=(char*)malloc(MAX_BUFFER*sizeof(char));
			printf("%c", buff[0]);	//print the char that haven't been printed bc of getch()
			temp_input= readline(ps); //read the remaining command
			strcat(buff, temp_input); //strcat the first character and the reamining command
			
			pre_is_arrow=false;		
		}
		
		/////////////////////////add buff to history stack/////////////////////////
		push(buff);  
	
		
		for (i=0; i<MAX_BUFFER && buff[i]!='\0'; i++){ //caculate buff size
			buff_size++;
			if(buff[i]=='>') redirect=true; //detect the redirection sign '>' or ">>"
		}

		/////////////////////////seperate the command and the conncted parameters(par1, par2, par3)/////////////////////////
		char command[MAX_BUFFER]="\0", par1[MAX_BUFFER]="\0", par2[50], par3[50]; 
		int index=0, outfd, outfd_back;
		 
		for(i=0; i<buff_size && buff[i]!='\0' && buff[i]!=' '; i++){
			command[i]=buff[i]; //get command
		}
		command[i]='\0'; i++;
		
		for (; i<buff_size && buff[i]!=' ' && buff[i]!='\0'; i++){
			par1[index]=buff[i]; //get parameter1
			index++;
		}
		par1[index]='\0'; 
		par2[0]='\0';

		if(buff[i]!='\0'){
			i++;
			for (index=0; i<buff_size && buff[i]!=' ' && buff[i]!='\0'; i++){
				par2[index]=buff[i]; //get parameter2
				index++;
			}
			par2[index]='\0';
		}
		
		par3[0]='\0';
		if(buff[i]!='\0'){
			i++;
			for (index=0; i<buff_size && buff[i]!=' ' && buff[i]!='\0'; i++){
				par3[index]=buff[i]; //get parameter3
				index++;
			}
			par3[index]='\0'; i++;
		}
		
		if(strcmp(par1, ">>")==0 || strcmp(par2, ">>")==0) append=true;
		
		/////////////////////////if it is redirection, open target file/////////////////////////
		if(redirect==true) { 
			fflush(stdout);
			outfd_back=dup(1); 
			//create/append output file
			if(par3[0]=='\0' && append==false) outfd=open(par2, O_CREAT|O_WRONLY|O_TRUNC, 0666); 
			else if(par3[0]!='\0' && append==false) outfd=open(par3, O_CREAT|O_WRONLY|O_TRUNC, 0666);
			else if (par3[0]=='\0' && append==true) outfd=open(par2, O_WRONLY|O_APPEND, 0666);
			else if (par3[0]!='\0' && append==true) outfd=open(par3, O_WRONLY|O_APPEND, 0666);

			if(!outfd){
				perror("error creating file\n");
				return EXIT_FAILURE;
			}
		}

		/////////////////////////background execution/////////////////////////
		if(buff[buff_size-1]=='&'){
			bg_finish=false; //background is not finished
			
			pid= fork();
			child_pid= getpid();
			if(pid<0){
				printf("fork process error\n");
				return 0;
			}
			else if(pid==0){ //background(child)
				buff[buff_size-2]='\0'; buff[buff_size-1]='\0'; //erase the ending '&'
			}
			else { //foreground(parent)
				printf("[%d] %d\n", ++process_num, getpid());
				buff[buff_size-2]='\0'; buff[buff_size-1]='\0';
				free(buff);
				continue;	
			}
		}

		/////////////////////////bg command/////////////////////////
		if(pid>0 && strcmp(buff, "bg")==0){
			int status;
			
			pid_t result= waitpid(pid, &status, WNOHANG);  //check if backgroud(child) process still exist
			if(result<0) { 
				bg_finish=true; //background has finished
			}

			if(bg_finish==true){ 
				printf("bash: bg: job has terminated\n");
				//printf("Done			%s\n", buff);
				process_num--;
				free(buff);
				continue;
			}
			else{ //background not finished
				printf("bash: bg: job %d already in background\n", process_num);
			}
				
		}
			
		/////////////////////////pwd command/////////////////////////
		if(strcmp(buff, "pwd")==0){	
			getcwd(curr_path, sizeof(curr_path));
			if(redirect==true){
				dup2(outfd, 1); //redirect output to file par2
				printf("%s\n", curr_path);
				fflush(stdout);
				dup2(outfd_back, 1);
				close(outfd);

			}
			else printf("%s\n", curr_path);
		}
		/////////////////////////echo command/////////////////////////
		else if(strncmp(buff, "echo ", 5)==0 && buff[5]=='"'){  //ex: echo "Hello"
			ECHO_COMMAND(0, outfd, outfd_back);		
		}
		else if(strncmp(buff, "echo ", 5)==0 && buff[5]=='$'){  //ex: echo $PATH
			ECHO_COMMAND(1, outfd, outfd_back);
		}
		else if(strncmp(buff, "echo ", 5)==0){  //ex: echo Hello
			ECHO_COMMAND(2, outfd, outfd_back);		
		}
		/////////////////////////cd command/////////////////////////		
		else if(strcmp(buff, "cd")==0){
			CD(0);
		}
		else if(strncmp(buff, "cd ", 3)==0){		
			CD(1);
		}
		/////////////////////////export command/////////////////////////
		else if (strncmp(buff, "export ", 7)==0){
			EXPORT();
		}
		/////////////////////////sleep command/////////////////////////
		else if(strncmp(buff, "sleep ", 6)==0){
			SLEEP();
		}
		else if(strcmp(buff, "exit")==0){ //press "exit" to exit this shell
			break;
		}
		/////////////////////////external command/////////////////////////
		else{ 
			pid_t ex_pid;
			ex_pid= fork();
			if(ex_pid<0){
				printf("fork process error\n");
				return 0;
			}
			else if(ex_pid==0){ //child process
				if(redirect==true){
					dup2(outfd, 1); //redirect output to file par2

					if(par3[0]!='\0') execlp(command, par3, par1, NULL); //with 2 par: ls -l >> file
					else execlp(command, par2, NULL); //with 1 par: ls > file
	
					fflush(stdout);
					dup2(outfd_back, 1);
					close(outfd);
				}
				else{
					if(par1[0]!='\0' && par2[0]!='\0') execlp(command, command, par1, par2, NULL); //with 2 par
					else if(par1[0]!='\0') execlp(command, command, par1, NULL); //with 1 par
					else execlp(command, command, NULL); //with no par
				}	
				return 0;
			}
			else { //parent process
				wait(NULL);
			}
		}
		free(buff);
		
		/////////////////////////background(child) process exits/////////////////////////
		if(pid==0) { 
			bg_finish=true;
			exit(0); //exit curr background(child) process		
		}
	}
	
	return 0;
}
