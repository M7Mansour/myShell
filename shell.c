#include <stdio.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <time.h>

#define clear() system("@clr || clear")        // clear the terminal clr for windows and clear for linux
#define cursorRight(x) printf("\033[%dC", (x)) // move cursor right by x moves
#define cursorLeft(x) printf("\033[%dD", (x))  // moves cursor left by x moves
#define backspace(x) printf("\033[%dP", (x))   // delete the last x letters before the cursor

// key codes values
#define KEY_ESCAPE 0x001b
#define KEY_ENTER 0x000a
#define KEY_UP 0x0041
#define KEY_DOWN 0x0042
#define KEY_RIGHT 0x0043
#define KEY_LEFT 0x0044
#define KEY_SPACE 0x0020
#define KEY_TAB 0x0009
#define BACK_SPACE 0x007F

// create 2 instances of the termios structure
static struct termios term, oterm;

extern char **environ;         // store environment variables in the environ varialbe
int bkgrndCount = 0;           // store how many background processes are runed by the shell
char pwd[4096];                // store the current directory
int fileInput;                 // detect for input redirection
char inputFileContent[100000]; // store the content of the input file
FILE *outStream;               // the output stream that we will write the output to

// modify the terminal variables so that when a key is pressed in it
// it will not wait for enter key hit to take the input key
int getChar()
{
    tcgetattr(0, &oterm);
    memcpy(&term, &oterm, sizeof(term));
    term.c_lflag &= ~(ICANON | ECHO);
    term.c_cc[VMIN] = 1;
    term.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &term);
    int key = getchar();
    tcsetattr(0, TCSANOW, &oterm);
    return key;
}

// special handling for arrow keys
int handleRowBtns()
{
    getChar();
    return getChar();
}

// get the inputed key code from the terminal
int getKey()
{
    int key = getChar();
    return key == KEY_ESCAPE ? handleRowBtns() : key;
}

// get file size of a given filename path
int fileSize(char *fileName)
{
    FILE *file = fopen(fileName, "r"); // open the file
    fseek(file, 0, SEEK_END);          // seek through the end of the file
    int size = ftell(file);            // get the position of the pointer from the end of the file
    fseek(file, 0, SEEK_SET);          // seek back to the start of the file
    fclose(file);                      // close the file
    return size;
}

int readFile(char *file)
{
    FILE *filePointer = fopen(file, "r"); // open the file for reading
    if (filePointer == NULL)              // exit if the file doesn't exist
    {
        printf("\n\033[0;31mmyshell: readfile: file %s does not exist\033[0m\n", file);
        return 0;
    }
    else
    {
        char ch;
        int i = 0;
        // read the file char by char and store them in the inputFileContent
        do
        {
            ch = fgetc(filePointer);
            if (ch == '\n' && inputFileContent[i - 1] == '\n')
                continue;
            inputFileContent[i++] = ch == EOF ? 0 : ch;
        } while (ch != EOF);
        fclose(filePointer);
        inputFileContent[i] = 0;
        return 1;
    }
}

// detects the command and give the runCommand function the args and input, output
// if there is any error it returns 0 else returns the number of args
int detectCommand(char command[], char *args[])
{
    int argCount = 0;                   // args number
    int argStop = 0;                    // endicates if there is more args or not
    int outStop = 0;                    // endicates if an output file is given yet
    char *token = strtok(command, " "); // split command by " "
    for (int i = 0; token != NULL && i < 1000; i++)
    {

        if (strcmp(token, "<") == 0)
        {
            if (argStop)
            {
                printf("\n\033[0;31mmyshell: argerror: invalid arguments\033[0m\n");
                return 0;
            }
            argStop = 1;
            token = strtok(NULL, " ");
            if (token != NULL && strcmp(token, ">") != 0 && strcmp(token, ">>") != 0)
            {
                int success = readFile(token);
                if (success)
                    fileInput = 1;
                else
                    return 0;
            }
            else
            {
                printf("\n\033[0;31mmyshell: argerror: input file argument not provided\033[0m\n");
                return 0;
            }
        }
        else if (strcmp(token, ">") == 0 || strcmp(token, ">>") == 0)
        {
            if (outStop)
            {
                printf("\n\033[0;31mmyshell: argerror: invalid arguments\033[0m\n");
                return 0;
            }
            char *outputMode = token;
            token = strtok(NULL, " ");
            if (token != NULL && strcmp(token, "<") != 0)
            {
                switch (strcmp(outputMode, ">"))
                {
                case 0:
                    outStream = fopen(token, "w");
                    break;

                default:
                    outStream = fopen(token, "a");
                    break;
                }
            }
            else
            {
                printf("\n\033[0;31mmyshell: argerror: output file argument not provided\033[0m\n");
                return 0;
            }
        }
        else if (!argStop)
            args[argCount++] = token;
        token = strtok(NULL, " ");
    }
    args[argCount] = NULL;
    return argCount;
}

// the core function of the shell which run all the commands the shell provide
void runCommand(char command[], int print)
{
    fileInput = 0;      // clear the file input status
    char *args[1001];   // declare the args array
    outStream = stdout; // put the stdout as output stream by default
    int argCount = detectCommand(command, args);
    if (!argCount) // if the command have any error it returns
        return;
    if (fileInput) // concat the input file content into the args so it will be part of the command
    {
        args[argCount++] = inputFileContent;
        args[argCount] = NULL;
    }

    if (print) // if the input is not from a file print a new line after each command
    {
        fprintf(outStream, "\n");
    }
    if (strcmp(args[0], "cd") == 0) // perform cd command
    {
        if (args[1] != NULL && args[2] != NULL)
        {
            fprintf(outStream, "\033[0;31mmyshell: cd: too many arguments\033[0m\n");
        }
        else if (args[1] == NULL)
            fprintf(outStream, "%s\n", pwd); // print the current directory
        else
        {
            int err = chdir(args[1]); // move to the directory provided by args[1]
            if (!err)
            {
                getcwd(pwd, sizeof(pwd));
                setenv("PWD", pwd, 1); // set pwd to the new current directory
            }
            else
                fprintf(outStream, "\033[0;31mmyshell: cd: %s: No such file or direcotry\033[0m\n", args[1]);
        }
    }
    else if (strcmp(args[0], "clr") == 0) // perform the clr command which clears the terminal
    {
        clear();
    }
    else if (strcmp(args[0], "dir") == 0) // perform the dir command
    {
        if (args[1] != NULL && args[2] != NULL)
        {
            fprintf(outStream, "\033[0;31mmyshell: dir: too many arguments\033[0m\n");
        }
        else
        {
            DIR *d;
            struct dirent *dir;
            char *directory = args[1] != NULL ? args[1] : "."; // detect which directory to dir
            d = opendir(directory);
            if (d) // if the directory exist read it
            {
                dir = readdir(d);
                while (dir != NULL)
                {
                    fprintf(outStream, "%s  ", dir->d_name); // get the d_name value from the dirnet stuct dir
                    dir = readdir(d);
                }
                fprintf(outStream, "\n");
                closedir(d);
            }
            else
                fprintf(outStream, "\033[0;31mmyshell: dir: cannot read directory %s\033[0m\n", directory);
        }
    }
    else if (strcmp(args[0], "environ") == 0)
    {
        char **env = environ;
        while (*env)
            fprintf(outStream, "%s\n", *env++);
    }
    else if (strcmp(args[0], "echo") == 0)
    {
        int commentSize = 0;
        for (int i = 1; i < argCount; i++)
            commentSize += strlen(args[i]) + 1;
        char comment[commentSize];
        comment[0] = 0;
        for (int i = 1; i < argCount; i++)
        {
            strcat(comment, args[i]);
            strcat(comment, " ");
        }
        fprintf(outStream, "%s\n", comment);
    }
    else if (strcmp(args[0], "pause") == 0)
    {
        fprintf(outStream, "\033[1;36mpress Enter to continue\033[0m\n");
        while (getKey() != KEY_ENTER)
            ;
    }
    else if (strcmp(args[0], "quit") == 0)
    {
        clear();
        exit(0);
    }
    else
    {
        bool bkgrndProc = strcmp(args[argCount - 1], "&") == 0;
        int pid = fork();
        if (!bkgrndProc)
        {
            waitpid(pid, NULL, 0);
        }
        else
            args[argCount - 1] = 0;
        if (pid == -1)
        {
            fprintf(outStream, "\033[0;31mproblem executing command %sc\n", args[0]);
        }
        else if (pid == 0)
        {
            setenv("parent", getenv("shell"), 1);
            int commandSize = 0;
            for (int i = 0; i < argCount; i++)
                commandSize += strlen(args[i]) + 1;
            char cmnd[commandSize];
            cmnd[0] = 0;
            for (int i = 0; i < argCount; i++)
            {
                strcat(cmnd, args[i]);
                strcat(cmnd, " ");
            }
            FILE *pipe = popen(cmnd, "r");
            char ch;
            if (pipe == NULL)
            {
                fprintf(outStream, "\033[0;31mcommand %s not found\033[0m\n", args[0]);
                exit(1);
            }
            while ((ch = fgetc(pipe)) != EOF)
                fprintf(outStream, "%c", ch);
            pclose(pipe);
        }
        else
        {
            if (bkgrndProc)
                fprintf(outStream, "[%d] %d\n", ++bkgrndCount, pid);
        }
    }
    if (outStream != stdout)
    {
        fclose(outStream);
        outStream = stdout;
        printf("\n");
    }
}

void main(int argc, char *argv[])
{
    signal(SIGINT, SIG_IGN);
    getcwd(pwd, sizeof(pwd));
    setenv("shell", pwd, 1);
    if (argv[1] != NULL)
    {
        if (argv[2] != NULL)
        {
            printf("\033[0;31mmyshell: error: too many arguments\033[0m\n");
        }
        else
        {
            FILE *filePointer = fopen(argv[1], "r");
            if (filePointer == NULL)
            {
                printf("\033[0;31merror! opening file %s\033[0m\n", argv[1]);
            }
            else
            {
                fseek(filePointer, 0, SEEK_END);
                int size = ftell(filePointer);
                fseek(filePointer, 0, SEEK_SET);
                char ch;
                int i = 0;
                char comm[size];
                do
                {
                    ch = fgetc(filePointer);
                    comm[i++] = ch == EOF ? 0 : ch;
                } while (ch != EOF);
                comm[i] = 0;
                fclose(filePointer);
                char *token = strtok(comm, "\n");
                char *commands[100000];
                i = 0;
                while (token != NULL)
                {
                    commands[i++] = token;
                    token = strtok(NULL, "\n");
                }
                commands[i] = 0;
                for (int j = 0; j < i; j++)
                {
                    runCommand(commands[j], 0);
                }
            }
        }
        exit(0);
    }

    clear();
    char history[500][200]; //store commands history
    int historyPointer = 0;

    while (1)
    {
        char command[200] = {0};
        int commandPointer = 0;
        int cursorPointer = 0;
        int currentHistory = historyPointer;

        printf("\033[1;32m%s\033[0m> ", pwd);

        while (1)
        {
            int key = getKey();
            if (key == KEY_UP)
            {
                if (currentHistory > 0)
                {
                    if (currentHistory == historyPointer)
                        strcpy(history[currentHistory], command);
                    currentHistory--;
                    cursorLeft(cursorPointer + 1);
                    backspace(commandPointer + 1);
                    putchar(' ');
                    cursorPointer = 0;
                    commandPointer = 0;
                    int i = 0;
                    while (history[currentHistory][i] != 0)
                    {
                        putchar(history[currentHistory][i]);
                        command[commandPointer++] = history[currentHistory][i++];
                        cursorPointer++;
                    }
                    command[commandPointer] = 0;
                }
            }
            else if (key == KEY_DOWN)
            {
                if (currentHistory < historyPointer)
                {
                    currentHistory++;
                    cursorLeft(cursorPointer + 1);
                    backspace(commandPointer + 1);
                    putchar(' ');
                    cursorPointer = 0;
                    commandPointer = 0;
                    int i = 0;
                    while (history[currentHistory][i] != 0)
                    {
                        putchar(history[currentHistory][i]);
                        command[commandPointer++] = history[currentHistory][i++];
                        cursorPointer++;
                    }
                    command[commandPointer] = 0;
                }
            }
            else if (key == BACK_SPACE)
            {
                if (cursorPointer > 0 && commandPointer > 0)
                {
                    int i = --cursorPointer;
                    while (command[i] != 0)
                    {
                        command[i++] = command[i];
                    }
                    commandPointer--;
                    cursorLeft(1);
                    backspace(1);
                }
            }
            else if (key == KEY_LEFT)
            {
                if (cursorPointer > 0)
                {
                    cursorPointer--;
                    cursorLeft(1);
                }
            }
            else if (key == KEY_RIGHT)
            {
                if (cursorPointer < commandPointer)
                {
                    cursorPointer++;
                    cursorRight(1);
                }
            }
            else if (key == KEY_ENTER)
            {
                command[commandPointer] = 0;
                if (strcmp(command, "") == 0)
                    printf("\n");
                else
                {
                    strcpy(history[historyPointer++], command);
                    runCommand(command, 1);
                }
                break;
            }
            else
            {
                if (commandPointer < 200)
                {
                    if (key == KEY_TAB)
                        key = KEY_SPACE;
                    putchar(key);
                    if (cursorPointer == commandPointer)
                    {
                        command[cursorPointer] = key;
                    }
                    else
                    {
                        int diff = commandPointer - cursorPointer;
                        int temp = key;
                        for (int i = 0; i <= diff; i++)
                        {
                            int temp1 = command[cursorPointer + i];
                            command[cursorPointer + i] = temp;
                            temp = temp1;
                            if (i != diff)
                                putchar(temp);
                        }
                        cursorLeft(diff);
                    }
                    cursorPointer++;
                    commandPointer++;
                    command[commandPointer] = 0;
                }
            }
        }
    }
}