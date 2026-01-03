This project is a Knowledge Graph Engine implemented in C that reads relationships between concepts from a text file and represents them in a structured form using
a Subject | Relation | Object format.. It demonstrates file handling, string parsing, and structured data storage in C.

->⚙️ How the Program Works

Reads input relations from a text file line by line

Uses | as a delimiter to separate entities and relationships

Stores each relation using C structures

Processes and displays the relationships

->📄 Type of Input Required

Input must be provided through a text file

Each line should contain exactly three parts:

->Entity1|Relation|Entity2


Use no extra delimiters or blank lines

Maintain the same format for all relations

📌 Use the provided sample relations file as a reference while creating your input.

->📥 Sample Input Format
Machine Learning|Requires|Python
Deep Learning|Subset of|Machine Learning
Python|Has Library|NumPy
Cyber Security|Requires|Ethical Hacking

->▶️ How to Run

Compile the program: gcc ipproject.c -o ipproject

Run the executable: ./ipproject

Ensure the input relations file is present in the same directory and the input is according to the format specified 
