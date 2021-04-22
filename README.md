# Mole
Credit project for the subject Operating Systems 1.
Program is written in pure C. 
Program goes through all the files in the given directory and subdirectories, creates a data structure in memory containing the necessary data about these files, and then waits for commands which are queries about the data contained in this structure. In order not to look through the whole directory each time, the whole data structure is saved to a file and loaded in subsequent calls to the program.
File types are recognized by file signature (magic number headers) Projects uses POSIX program execution environment, threads, subprocesses and low-level IO.  
