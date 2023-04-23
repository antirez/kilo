Kilo
===

Kilo is a small text editor in less than 1K lines of code (counted with cloc).

A screencast is available here: https://asciinema.org/a/90r2i9bq8po03nazhqtsifksb

Usage: kilo `<filename>`
New Usage: kilo `<host>` `<port>`

'get' to copy file from server

Editor Keys:

    CTRL-S: Save
    CTRL-Q: Quit
    CTRL-F: Find string in file (ESC to exit search, arrows to navigate)

Kilo does not depend on any library (not even curses). It uses fairly standard
VT100 (and similar terminals) escape sequences. The project is in alpha
stage and was written in just a few hours taking code from my other two
projects, load81 and linenoise.

People are encouraged to use it as a starting point to write other editors
or command line interfaces that are more advanced than the usual REPL
style CLI.

Kilo was written by Salvatore Sanfilippo aka antirez and is released
under the BSD 2 clause license.

# cpd-term-project
Term Project for COP5570
Concurrent Text File Editing (Google Docs redev)

Contributers:
Skylar Scorca,
Tony Drouillard,
Jack Dewey

Project Objectives (intended features)
- Allow users to update a single text file simultaneously
- Allow users to view the updates of other users in real-time
- Handle the movement of a user’s cursor after an update is made
- Handle transactions quickly and efficiently

Optional Objectives: (if time allows)
- Allow users to create and delete text files in a greater system of files
- Allow users to modify the tree structure of the greater system of files
- Allow users to view the updates to the file system in real-time

Due date countdown:
https://www.timeanddate.com/countdown/generic?iso=20230428T2359&p0=856&msg=cpd+term+project+due+date&font=cursive&csz=1

Google Drive Folder:
https://drive.google.com/drive/folders/1sZ5ulUizpBA6Rq9KOYdHxhaF4QC_5UAi?usp=share_link
