#!/usr/bin/env python3
# Copyright 2008 Marcus D. Hanwell <marcus@cryos.org>
# Minor changes for NUT by Charles Lepple
# Distributed under the terms of the GNU General Public License v2 or later

import re
from textwrap import TextWrapper
import sys
import subprocess

rev_range = ""


# Define the git command and its arguments as a list
git_command = [
    "git",
    "log",
    "--summary",
    "--stat",
    "--no-merges",
    "--date=short",
]

if len(sys.argv) > 1:
    base = sys.argv[1]
    rev_range = "%s..HEAD" % base
    git_command.append(rev_range)

# Execute git log with the desired command line options.
process = subprocess.Popen(git_command, stdout=subprocess.PIPE, encoding="utf8")
fin = process.stdout

# Create a ChangeLog file in the current directory.
fout = open("ChangeLog", "w")

# Set up the loop variables in order to locate the blocks we want
authorFound = False
dateFound = False
messageFound = False
filesFound = False
message = ""
messageNL = False
files = ""
prevAuthorLine = ""

wrapper = TextWrapper(initial_indent="\t", subsequent_indent="\t  ")

# The main part of the loop
for line in fin:
    # The commit line marks the start of a new commit object.
    if line.startswith("commit"):
        # Start all over again...
        authorFound = False
        dateFound = False
        messageFound = False
        messageNL = False
        message = ""
        filesFound = False
        files = ""
        continue
    # Match the author line and extract the part we want
    # (Don't use startswith to allow Author override inside commit message.)
    elif "Author:" in line:
        authorList = re.split(": ", line, 1)
        try:
            author = authorList[1]
            author = author[0 : len(author) - 1]
            authorFound = True
        except:
            print("Could not parse authorList = '%s'" % (line))

    # Match the date line
    elif line.startswith("Date:"):
        dateList = re.split(":   ", line, 1)
        try:
            date = dateList[1]
            date = date[0 : len(date) - 1]
            dateFound = True
        except:
            print("Could not parse dateList = '%s'" % (line))
    # The Fossil-IDs are ignored:
    elif line.startswith("    Fossil-ID:") or line.startswith("    [[SVN:"):
        continue
    # The svn-id lines are ignored
    elif "    git-svn-id:" in line:
        continue
    # The sign off line is ignored too
    elif "Signed-off-by" in line:
        continue
    # Extract the actual commit message for this commit
    elif authorFound & dateFound & messageFound is False:
        # Find the commit message if we can
        if len(line) == 1:
            if messageNL:
                messageFound = True
            else:
                messageNL = True
        elif len(line) == 4:
            messageFound = True
        elif len(message) == 0:
            message += line.strip()
        else:
            message = message + " " + line.strip()
    # If this line is hit all of the files have been stored for this commit
    elif re.search("files? changed", line):
        filesFound = True
        continue
    # Collect the files for this commit. FIXME: Still need to add +/- to files
    elif authorFound & dateFound & messageFound:
        fileList = re.split(r" \| ", line, 2)
        if len(fileList) > 1:
            if len(files) > 0:
                files = files + ", " + fileList[0].strip()
            else:
                files = fileList[0].strip()
    # All of the parts of the commit have been found - write out the entry
    if authorFound & dateFound & messageFound & filesFound:
        # First the author line, only outputted if it is the first for that
        # author on this day
        authorLine = date + "  " + author
        if len(prevAuthorLine) == 0:
            fout.write(authorLine + "\n\n")
        elif authorLine == prevAuthorLine:
            pass
        else:
            fout.write("\n" + authorLine + "\n\n")

        # Assemble the actual commit message line(s) and limit the line length
        # to 80 characters.
        commitLine = "* " + files + ": " + message

        # Write out the commit line
        fout.write(wrapper.fill(commitLine) + "\n")

        # Now reset all the variables ready for a new commit block.
        authorFound = False
        dateFound = False
        messageFound = False
        messageNL = False
        message = ""
        filesFound = False
        files = ""
        prevAuthorLine = authorLine

# Close the input and output lines now that we are finished.
fin.close()
fout.close()
