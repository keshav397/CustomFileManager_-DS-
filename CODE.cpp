#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <deque>
#include <thread> //allowing the program to explicitly control its own execution state.
#include <chrono> //allowing the compiler to understand exact time measurements like milliseconds.
#ifdef _WIN32
#include <conio.h>
#else
#include <cstdio>
#include <termios.h>
#include <unistd.h>
#endif
using namespace std;

struct Theme
{
    string name;
    string dirColor;
    string fileColor;
    string accentColor;
    string promptColor;
    string reset;
};

void bootSequence()
{
// Just removing everything else from the screen
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif

    cout << "  _   _                   _____ ____  \n";
    cout << " | \\ | | __ _ _ __   ___ |  ___/ ___| \n";
    cout << " |  \\| |/ _` | '_ \\ / _ \\| |_  \\___ \\ \n";
    cout << " | |\\  | (_| | | | | (_) |  _|  ___) |\n";
    cout << " |_| \\_|\\__,_|_| |_|\\___/|_|   |____/ \n\n";

    cout << " Booting NanoFS Kernel v1.0...\n";
    cout << " [";
    // 3. The Animated Loading Bar
    for (int i = 0; i < 20; i++)
    {
        cout << "#" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    cout << "] 100%\n";
    cout << " Welcome to NanoFS.\n\n";
}

int getCharRaw()
{
#ifdef _WIN32
    return _getch();
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
#endif
}

class FileNode
{
public:
    string name;
    bool isDirectory;
    string content = "";
    FileNode *parent; // for cd .. command
    unordered_map<string, FileNode *> children;

    FileNode(string nodeName, bool isDir, FileNode *parentNode = nullptr)
    {
        name = nodeName;
        isDirectory = isDir;
        parent = parentNode;
    }

    ~FileNode()
    {
        for (auto const &p : children)
        {
            delete p.second;
        }
    }
};

struct TrieNode
{
    TrieNode *children[128];
    bool isEndOfWord;

    TrieNode()
    {
        isEndOfWord = false;
        for (int i = 0; i < 128; i++)
        {
            children[i] = nullptr;
        }
    }

    ~TrieNode()
    {
        for (int i = 0; i < 128; i++)
        {
            if (children[i] != nullptr)
            {
                delete children[i];
            }
        }
    }
};

// implementation of MKDIR
class FileSystem
{
private:
    FileNode *root;
    FileNode *currentDir;
    deque<string> commandHistory;
    unordered_map<string, Theme> themes;
    Theme currentTheme;

    FileNode *resolvePath(const string &path)
    {
        if (path.empty())
            return nullptr;
        if (path == "/")
            return root;

        FileNode *curr = (path[0] == '/') ? root : currentDir;
        size_t i = (path[0] == '/') ? 1 : 0;

        while (i < path.size())
        {
            string part = "";
            while (i < path.size() && path[i] != '/')
            {
                part += path[i];
                i++;
            }
            i++;

            if (part.empty() || part == ".")
                continue;
            if (part == "..")
            {
                if (curr->parent != nullptr)
                    curr = curr->parent;
            }
            else
            {
                if (!curr->isDirectory || !curr->children.contains(part))
                    return nullptr;
                curr = curr->children[part];
            }
        }
        return curr;
    }

    bool resolveParentAndName(const string &rawPath, FileNode *&parentOut, string &nameOut)
    {
        string path = rawPath;

        while (path.size() > 1 && path.back() == '/')
        {
            path.pop_back();
        }

        if (path.empty() || path == "/")
        {
            return false;
        }

        size_t lastSlash = path.rfind('/');

        if (lastSlash == string::npos)
        {
            parentOut = currentDir;
            nameOut = path;
        }
        else
        {
            string parentPath;
            if (lastSlash == 0)
            {
                parentPath = "/";
            }
            else
            {
                parentPath = path.substr(0, lastSlash);
            }

            parentOut = resolvePath(parentPath);
            nameOut = path.substr(lastSlash + 1);
        }

        if (parentOut == nullptr || !parentOut->isDirectory)
        {
            return false;
        }

        if (nameOut.empty() || nameOut == "." || nameOut == "..")
        {
            return false;
        }

        return true;
    }

    bool isAncestor(FileNode *potentialAncestor, FileNode *node)
    {
        FileNode *temp = node;

        while (temp != nullptr)
        {
            if (temp == potentialAncestor)
            {
                return true;
            }
            temp = temp->parent;
        }

        return false;
    }

    FileNode *cloneNode(FileNode *src, FileNode *newParent)
    {
        FileNode *copy = new FileNode(src->name, src->isDirectory, newParent);
        copy->content = src->content;

        for (auto const &p : src->children)
        {
            copy->children[p.first] = cloneNode(p.second, copy);
        }
        return copy;
    }

    vector<string> splitArgs(const string &str)
    {
        vector<string> tokens;
        stringstream ss(str);
        string word;
        while (ss >> word)
        {
            tokens.push_back(word);
        }
        return tokens;
    }

    void trieInsert(TrieNode *tRoot, const string &word)
    {
        TrieNode *curr = tRoot;
        for (char c : word)
        {
            unsigned char idx = static_cast<unsigned char>(c);
            if (idx >= 128)
                continue;
            if (curr->children[idx] == nullptr)
            {
                curr->children[idx] = new TrieNode();
            }
            curr = curr->children[idx];
        }
        curr->isEndOfWord = true;
    }

    vector<string> autoCompleteMatches(const string &prefix)
    {
        vector<string> matches;
        if (prefix.empty())
        {
            return matches;
        }
        TrieNode *tRoot = new TrieNode();
        vector<string> commands = {
            "mkdir", "touch", "ls", "cd", "pwd", "rm", "trash", "restore",
            "write", "empty", "cat", "save", "load", "tree", "history",
            "clear", "grep", "nanofetch", "shred", "find", "du",
            "cp", "mv", "suggest", "exit", "props", "theme"};
        for (const string &cmd : commands)
            trieInsert(tRoot, cmd);
        indexTreeIntoTrie(root, "/", tRoot);
        TrieNode *curr = tRoot;
        for (char c : prefix)
        {
            unsigned char idx = static_cast<unsigned char>(c);
            if (idx >= 128 || curr->children[idx] == nullptr)
            {
                delete tRoot;
                return matches;
            }
            curr = curr->children[idx];
        }
        trieCollectWords(curr, prefix, matches);
        delete tRoot;
        return matches;
    }

    string readInputWithTab(const string &prompt)
    {
        string input = "";
        cout << prompt << flush;

        while (true)
        {
            int ch = getCharRaw();
            if (ch == '\r' || ch == '\n')
            {
                cout << '\n';
                return input;
            }
            else if (ch == 3)
            {
                cout << "^C\n";
                return "exit";
            }
            else if (ch == 8 || ch == 127)
            {
                if (!input.empty())
                {
                    input.pop_back();
                    cout << "\b \b" << flush;
                }
            }
            else if (ch == '\t')
            {
                size_t lastSpace = input.rfind(' ');
                string lastWord = (lastSpace == string::npos) ? input : input.substr(lastSpace + 1);
                vector<string> matches = autoCompleteMatches(lastWord);

                if (matches.size() == 1)
                {
                    string completion = matches[0].substr(lastWord.size());
                    input += completion;
                    cout << completion << flush;
                }
                else if (matches.size() > 1)
                {
                    cout << '\n';
                    for (const string &m : matches)
                    {
                        cout << currentTheme.fileColor << m << currentTheme.reset << "  ";
                    }
                    cout << '\n'
                         << prompt << input << flush;
                }
            }
#ifdef _WIN32
            else if (ch == 0 || ch == 224)
            {
                getCharRaw();
            }
#else
            else if (ch == 27)
            {
                getCharRaw();
                getCharRaw();
            }
#endif
            else if (ch >= 32 && ch <= 126)
            {
                input += static_cast<char>(ch);
                cout << static_cast<char>(ch) << flush;
            }
        }
    }

    void countLinesAndWords(const string &text, int &lines, int &words)
    {
        lines = 0;
        words = 0;
        if (text.empty())
        {
            return;
        }

        for (char c : text)
        {
            if (c == '\n')
            {
                lines++;
            }
        }
        if (text.back() != '\n')
        {
            lines++;
        }

        stringstream ss(text);
        string word;
        while (ss >> word)
        {
            words++;
        }
    }

    void countSubtreeItems(FileNode *node, int &subDirs, int &subFiles)
    {
        for (auto const &p : node->children)
        {
            FileNode *child = p.second;
            if (child->isDirectory)
            {
                subDirs++;
                countSubtreeItems(child, subDirs, subFiles);
            }
            else
            {
                subFiles++;
            }
        }
    }

public:
    FileSystem()
    {
        root = new FileNode("/", true);
        currentDir = root;
        root->children[".trash"] = new FileNode(".trash", true, root);
        themes["default"] = {"default", "\033[1;34m", "\033[0;32m", "\033[1;36m", "\033[0;32m", "\033[0m"};
        themes["matrix"] = {"matrix", "\033[1;32m", "\033[0;32m", "\033[1;32m", "\033[1;32m", "\033[0m"};
        themes["cyberpunk"] = {"cyberpunk", "\033[1;35m", "\033[1;36m", "\033[1;33m", "\033[1;35m", "\033[0m"};
        themes["amber"] = {"amber", "\033[1;33m", "\033[0;33m", "\033[1;31m", "\033[1;33m", "\033[0m"};
        themes["ocean"] = {"ocean", "\033[1;36m", "\033[1;34m", "\033[1;37m", "\033[1;36m", "\033[0m"};
        currentTheme = themes["default"];
    }

    ~FileSystem()
    {
        delete root;
    }

    // creating new directory
    void mkdir(string dirPath)
    {
        FileNode *parentDir = nullptr;
        string dirName;
        if (!resolveParentAndName(dirPath, parentDir, dirName))
        {
            cout << "mkdir: invalid path '" << dirPath << "'\n";
            return;
        }
        if (parentDir->children.contains(dirName))
        {
            cout << "mkdir: cannot create directory " << dirName << " : File Already Exists\n";
            return;
        }
        FileNode *newDir = new FileNode(dirName, true, parentDir);
        parentDir->children[dirName] = newDir;
    }

    void touch(string filePath)
    {
        FileNode *parentDir = nullptr;
        string fileName;
        if (!resolveParentAndName(filePath, parentDir, fileName))
        {
            cout << "touch: invalid path '" << filePath << "'\n";
            return;
        }
        if (parentDir->children.contains(fileName))
        {
            cout << "touch: cannot create file " << fileName << " : File Already Exists\n";
            return;
        }
        FileNode *newFile = new FileNode(fileName, false, parentDir);
        parentDir->children[fileName] = newFile;
    }

    void ls(string path = ".")
    {
        FileNode *target = resolvePath(path);
        if (target == nullptr)
        {
            cout << "ls: path '" << path << "' doesn't exist\n";
            return;
        }
        if (!target->isDirectory)
        {
            cout << currentTheme.fileColor << target->name << currentTheme.reset << "\n";
            return;
        }
        for (auto const &p : target->children)
        {
            if (p.second->isDirectory)
                cout << currentTheme.dirColor << p.first << currentTheme.reset << "\n";
            else
                cout << currentTheme.fileColor << p.first << currentTheme.reset << "\n";
        }
    }

    void lsWildcard(string pattern)
    {
        string ext = "";
        if (!pattern.empty() && pattern[0] == '*')
        {
            ext = pattern.substr(1);
        }
        else
        {
            ext = pattern;
        }
        for (auto const &p : currentDir->children)
        {
            if (p.first.size() >= ext.size() && p.first.substr(p.first.size() - ext.size()) == ext)
            {
                if (p.second->isDirectory)
                    cout << currentTheme.dirColor << p.first << currentTheme.reset << "\n";
                else
                    cout << currentTheme.fileColor << p.first << currentTheme.reset << "\n";
            }
        }
    }

    void cd(string dirPath = "/")
    {
        FileNode *target = resolvePath(dirPath);
        if (target == nullptr || !target->isDirectory)
        {
            cout << "Directory: " << dirPath << " doesn't exist\n";
            return;
        }
        currentDir = target;
    }

    void pathFinder(FileNode *dir, string &path)
    {
        if (dir == root)
        {
            path = "/";
            return;
        }
        pathFinder(dir->parent, path);
        if (path == "/")
        {
            path += dir->name;
        }
        else
        {
            path += ("/" + dir->name);
        }
    }

    void pwd()
    {
        string path = "";
        pathFinder(currentDir, path);
        cout << path << '\n';
    }

    void rm(string targetPath)
    {
        FileNode *target = resolvePath(targetPath);
        if (target == nullptr)
        {
            cout << "File : " << targetPath << " Doesn't exist\n";
            return;
        }
        if (target == root || target == root->children[".trash"])
        {
            cout << "Cannot remove system directory\n";
            return;
        }
        if (isAncestor(target, currentDir))
        {
            cout << "Cannot remove current or parent working directory\n";
            return;
        }
        target->parent->children.erase(target->name);
        delete target;
    }

    void trash(string targetPath)
    {
        FileNode *target = resolvePath(targetPath);
        if (target == nullptr)
        {
            cout << "File : " << targetPath << " Doesn't exist\n";
            return;
        }
        if (target == root || target == root->children[".trash"])
        {
            cout << "Cannot move system directory to trash\n";
            return;
        }
        if (isAncestor(target, currentDir))
        {
            cout << "Cannot trash current or parent working directory\n";
            return;
        }

        FileNode *trashDir = root->children[".trash"];
        if (trashDir->children.contains(target->name))
        {
            delete trashDir->children[target->name];
        }

        target->parent->children.erase(target->name);
        trashDir->children[target->name] = target;
        target->parent = trashDir;
    }

    void restore(string targetName)
    {
        if (!root->children[".trash"]->children.contains(targetName))
        {
            cout << "File : " << targetName << " not found in trash\n";
            return;
        }

        if (currentDir->children.contains(targetName))
        {
            cout << "Cannot restore: '" << targetName << "' already exists in this directory.\n";
            return;
        }

        FileNode *temp = root->children[".trash"]->children[targetName];
        currentDir->children[targetName] = temp;
        temp->parent = currentDir;
        root->children[".trash"]->children.erase(targetName);
    }

    void write(string filePath, const string &text)
    {
        FileNode *target = resolvePath(filePath);
        if (target == nullptr)
        {
            cout << "File : " << filePath << " Doesn't exist\n";
            return;
        }
        if (target->isDirectory)
        {
            cout << filePath << " is a Directory!\n";
            return;
        }

        target->content += (text + "\n");
    }

    void empty(string filePath)
    {
        FileNode *target = resolvePath(filePath);
        if (target == nullptr)
        {
            cout << "File : " << filePath << " Doesn't exist\n";
            return;
        }
        if (target->isDirectory)
        {
            cout << filePath << " is a Directory!\n";
            return;
        }

        target->content = "";
    }

    void cat(string filePath)
    {
        FileNode *target = resolvePath(filePath);
        if (target == nullptr)
        {
            cout << "File : " << filePath << " Doesn't exist\n";
            return;
        }
        if (target->isDirectory)
        {
            cout << filePath << " is a Directory!\n";
            return;
        }
        cout << target->content << '\n';
    }

    string escapeNewlines(const string &str)
    {
        string result = "";
        for (char c : str)
        {
            if (c == '\n')
            {
                result += "\\n";
            }
            else
            {
                result += c;
            }
        }
        return result;
    }

    void saveHelper(FileNode *node, string path, ofstream &outFile)
    {
        if (path != "/")
        {
            path += "/";
        }
        for (auto const &i : node->children)
        {
            string childPath = path + i.first;
            // write to file path + fileName;
            if (i.second->isDirectory)
            {
                outFile << "D " << childPath << '\n';
                saveHelper(i.second, childPath, outFile);
            }
            else
            {
                outFile << "F " << childPath << " " << escapeNewlines(i.second->content) << '\n';
            }
        }
    }

    void save()
    {
        ofstream outFile("data.txt");
        if (!outFile.is_open())
        {
            cout << "Could not open the file\n";
            return;
        }
        saveHelper(root, "", outFile);

        outFile.close();
        cout << "Saved Successfully\n";
    }

    string unescapeNewLines(const string &content)
    {
        string result = "";
        for (size_t i = 0; i < content.size(); i++)
        {
            if (content[i] == '\\' && i < content.size() - 1 && content[i + 1] == 'n')
            {
                result += '\n';
                i++;
            }
            else
            {
                result += content[i];
            }
        }
        return result;
    }

    vector<string> splitPaths(const string &path)
    {
        vector<string> parts;
        string s = "";
        for (size_t i = 1; i < path.size(); i++)
        {
            if (path[i] == '/')
            {
                parts.push_back(s);
                s = "";
            }
            else
                s += path[i];
        }
        if (!s.empty())
        {
            parts.push_back(s);
        }
        return parts;
    }

    bool load()
    {
        ifstream inFile("data.txt");
        if (!inFile.is_open())
        {
            cout << "Could not open the file\n";
            return false;
        }
        delete root;
        root = new FileNode("/", true);
        root->children[".trash"] = new FileNode(".trash", true, root);
        currentDir = root;
        string type, path;
        while (inFile >> type >> path)
        {
            string content = "";
            if (type == "F")
            {
                getline(inFile, content);
                if (!content.empty() && content[0] == ' ')
                {
                    content = content.substr(1);
                }
                content = unescapeNewLines(content);
            }
            vector<string> paths = splitPaths(path);
            FileNode *temp = root;
            for (size_t i = 0; i < paths.size(); i++)
            {
                if (temp->children.contains(paths[i]))
                {
                    temp = temp->children[paths[i]];
                }
                else
                {
                    if (i != paths.size() - 1 || type == "D")
                    {
                        currentDir = temp;
                        mkdir(paths[i]);
                        temp = temp->children[paths[i]];
                    }
                    else
                    {
                        currentDir = temp;
                        touch(paths[i]);
                        temp->children[paths[i]]->content = content;
                    }
                }
            }
        }
        currentDir = root;
        return true;
    }

    void treeHelper(FileNode *node, int depth)
    {
        for (auto const &i : node->children)
        {
            for (int j = 0; j < depth; j++)
            {
                cout << '\t';
            }
            if (i.second->isDirectory)
            {
                cout << "|-- " << currentTheme.dirColor << i.first << currentTheme.reset << '\n';
                treeHelper(i.second, depth + 1);
            }
            else
            {
                cout << "|-- " << currentTheme.fileColor << i.first << currentTheme.reset << '\n';
            }
        }
    }

    void tree()
    {
        cout << "/\n";
        treeHelper(root, 0);
    }

    void history()
    {
        for (size_t i = 0; i < commandHistory.size(); i++)
        {
            cout << i + 1 << "  " << commandHistory[i] << '\n';
        }
    }

    void clearScreen()
    {
#ifdef _WIN32
        system("cls");
#else
        system("clear");
#endif
    }

    void grepHelper(FileNode *node, string currentPath, const string &keyword, bool &found)
    {
        for (auto const &p : node->children)
        {
            FileNode *child = p.second;
            string nextPath = (currentPath == "/") ? ("/" + child->name) : (currentPath + "/" + child->name);

            if (child->isDirectory)
            {
                if (child->name != ".trash")
                    grepHelper(child, nextPath, keyword, found);
            }
            else
            {
                if (child->content.find(keyword) != string::npos)
                {
                    cout << currentTheme.fileColor << nextPath << currentTheme.reset << "\n";
                    found = true;
                }
            }
        }
    }

    void grep(const string &keyword)
    {
        bool found = false;
        grepHelper(root, "/", keyword, found);
        if (!found)
            cout << "No matches found for: " << keyword << "\n";
    }

    void statsHelper(FileNode *node, int &dirCount, int &fileCount, int &totalBytes)
    {
        for (auto const &p : node->children)
        {
            FileNode *child = p.second;
            if (child->name == ".trash")
                continue;
            if (child->isDirectory)
            {
                dirCount++;
                statsHelper(child, dirCount, fileCount, totalBytes);
            }
            else
            {
                fileCount++;
                totalBytes += child->content.length();
            }
        }
    }

    void nanofetch()
    {
        int dirCount = 0;
        int fileCount = 0;
        int totalBytes = 0;
        statsHelper(root, dirCount, fileCount, totalBytes);

        string currentPath = "";
        pathFinder(currentDir, currentPath);

        cout << "\n";
        cout << currentTheme.accentColor << "   _  __              ___  ___ " << currentTheme.reset << "  " << currentTheme.promptColor << "user@nanofs" << currentTheme.reset << "\n";
        cout << currentTheme.accentColor << "  / |/ /__ ____  ___ / _/ / __/" << currentTheme.reset << "  " << "--------------------" << "\n";
        cout << currentTheme.accentColor << " /    / _ `/ _ \\/ _ \\ _/_ \\ \\  " << currentTheme.reset << "  " << currentTheme.dirColor << "FS: " << currentTheme.reset << "NanoFS v1.0\n";
        cout << currentTheme.accentColor << "/_/|_/\\_,_/_//_/\\___/_/ /___/  " << currentTheme.reset << "  " << currentTheme.dirColor << "Path: " << currentTheme.reset << currentPath << "\n";
        cout << currentTheme.accentColor << "                               " << currentTheme.reset << "  " << currentTheme.dirColor << "Directories: " << currentTheme.reset << dirCount << "\n";
        cout << currentTheme.accentColor << "                               " << currentTheme.reset << "  " << currentTheme.dirColor << "Files: " << currentTheme.reset << fileCount << "\n";
        cout << currentTheme.accentColor << "                               " << currentTheme.reset << "  " << currentTheme.dirColor << "Used Space: " << currentTheme.reset << totalBytes << " Bytes\n";
        cout << "\n";
    }

    void cpSingle(const string &srcPath, const string &destPath)
    {
        FileNode *srcNode = resolvePath(srcPath);
        if (srcNode == nullptr)
        {
            cout << "cp: cannot find '" << srcPath << "'\n";
            return;
        }
        if (srcNode == root || srcNode->name == ".trash")
        {
            cout << "cp: cannot copy system directory\n";
            return;
        }

        FileNode *destNode = resolvePath(destPath);
        if (destNode != nullptr)
        {
            if (!destNode->isDirectory)
            {
                cout << "cp: destination '" << destPath << "' already exists as a file\n";
                return;
            }
            if (isAncestor(srcNode, destNode))
            {
                cout << "cp: cannot copy a directory into itself\n";
                return;
            }
            if (destNode->children.contains(srcNode->name))
            {
                cout << "cp: '" << srcNode->name << "' already exists in destination\n";
                return;
            }
            FileNode *cloned = cloneNode(srcNode, destNode);
            destNode->children[srcNode->name] = cloned;

            string createdPath = "";
            pathFinder(cloned, createdPath);
            cout << currentTheme.fileColor << "Copied '" << srcPath << "' -> '" << createdPath << "'" << currentTheme.reset << "\n";
        }
        else
        {
            FileNode *destParent = nullptr;
            string newName = "";
            if (!resolveParentAndName(destPath, destParent, newName))
            {
                cout << "cp: invalid destination path '" << destPath << "'\n";
                return;
            }
            if (isAncestor(srcNode, destParent))
            {
                cout << "cp: cannot copy a directory into itself\n";
                return;
            }
            if (destParent->children.contains(newName))
            {
                cout << "cp: '" << newName << "' already exists\n";
                return;
            }
            FileNode *cloned = cloneNode(srcNode, destParent);
            cloned->name = newName;
            destParent->children[newName] = cloned;

            string createdPath = "";
            pathFinder(cloned, createdPath);
            cout << currentTheme.fileColor << "Copied '" << srcPath << "' -> '" << createdPath << "'" << currentTheme.reset << "\n";
        }
    }

    void cp(const string &rawArgs)
    {
        vector<string> args = splitArgs(rawArgs);
        if (args.size() < 2)
        {
            cout << "Usage: cp <src1> [src2 ...] <destination>\n";
            return;
        }
        string dest = args.back();
        if (args.size() > 2)
        {
            FileNode *destNode = resolvePath(dest);
            if (destNode == nullptr || !destNode->isDirectory)
            {
                cout << "cp: target '" << dest << "' must be an existing directory for multiple files\n";
                return;
            }
        }
        for (int i = 0; i < args.size() - 1; i++)
        {
            cpSingle(args[i], dest);
        }
    }

    void mvSingle(const string &srcPath, const string &destPath)
    {
        FileNode *srcNode = resolvePath(srcPath);
        if (srcNode == nullptr)
        {
            cout << "mv: cannot find '" << srcPath << "'\n";
            return;
        }
        if (srcNode == root || srcNode->name == ".trash")
        {
            cout << "mv: cannot move system directory\n";
            return;
        }

        string oldFullPath = "";
        pathFinder(srcNode, oldFullPath);

        FileNode *destNode = resolvePath(destPath);
        if (destNode != nullptr)
        {
            if (!destNode->isDirectory)
            {
                cout << "mv: destination '" << destPath << "' already exists as a file\n";
                return;
            }
            if (isAncestor(srcNode, destNode))
            {
                cout << "mv: cannot move a directory inside itself (cycle detected)\n";
                return;
            }
            if (destNode->children.contains(srcNode->name))
            {
                cout << "mv: '" << srcNode->name << "' already exists in destination\n";
                return;
            }

            srcNode->parent->children.erase(srcNode->name);
            srcNode->parent = destNode;
            destNode->children[srcNode->name] = srcNode;

            string newFullPath = "";
            pathFinder(srcNode, newFullPath);
            cout << currentTheme.fileColor << "Moved '" << oldFullPath << "' -> '" << newFullPath << "'" << currentTheme.reset << "\n";
        }
        else
        {
            FileNode *destParent = nullptr;
            string newName = "";
            if (!resolveParentAndName(destPath, destParent, newName))
            {
                cout << "mv: invalid destination path '" << destPath << "'\n";
                return;
            }
            if (isAncestor(srcNode, destParent))
            {
                cout << "mv: cannot move a directory inside itself (cycle detected)\n";
                return;
            }
            if (destParent->children.contains(newName))
            {
                cout << "mv: '" << newName << "' already exists\n";
                return;
            }

            srcNode->parent->children.erase(srcNode->name);
            srcNode->name = newName;
            srcNode->parent = destParent;
            destParent->children[newName] = srcNode;

            string newFullPath = "";
            pathFinder(srcNode, newFullPath);
            cout << currentTheme.fileColor << "Moved/Renamed '" << oldFullPath << "' -> '" << newFullPath << "'" << currentTheme.reset << "\n";
        }
    }

    void mv(const string &rawArgs)
    {
        vector<string> args = splitArgs(rawArgs);
        if (args.size() < 2)
        {
            cout << "Usage: mv <src1> [src2 ...] <destination>\n";
            return;
        }
        string dest = args.back();
        if (args.size() > 2)
        {
            FileNode *destNode = resolvePath(dest);
            if (destNode == nullptr || !destNode->isDirectory)
            {
                cout << "mv: target '" << dest << "' must be an existing directory for multiple files\n";
                return;
            }
        }
        for (int i = 0; i < args.size() - 1; i++)
        {
            mvSingle(args[i], dest);
        }
    }

    void indexTreeIntoTrie(FileNode *node, string currentPath, TrieNode *tRoot)
    {
        for (auto const &p : node->children)
        {
            FileNode *child = p.second;
            if (child->name == ".trash")
                continue;

            trieInsert(tRoot, child->name);
            string nextPath = (currentPath == "/") ? ("/" + child->name) : (currentPath + "/" + child->name);
            trieInsert(tRoot, nextPath);

            if (child->isDirectory)
            {
                indexTreeIntoTrie(child, nextPath, tRoot);
            }
        }
    }

    void trieCollectWords(TrieNode *node, string currentWord, vector<string> &results)
    {
        if (node->isEndOfWord)
        {
            results.push_back(currentWord);
        }
        for (int i = 0; i < 128; i++)
        {
            if (node->children[i] != nullptr)
            {
                trieCollectWords(node->children[i], currentWord + static_cast<char>(i), results);
            }
        }
    }

    void suggest(const string &prefix)
    {
        vector<string> matches = autoCompleteMatches(prefix);
        if (matches.empty())
        {
            cout << "No suggestions matching '" << prefix << "'\n";
            return;
        }

        cout << currentTheme.accentColor << "Suggestions for '" << prefix << "':" << currentTheme.reset << "\n";
        for (const string &m : matches)
        {
            cout << "  -> " << currentTheme.fileColor << m << currentTheme.reset << "\n";
        }
    }

    // 1)
    void shred(string filePath)
    {
        FileNode *target = resolvePath(filePath);
        if (target == nullptr)
        {
            cout << "Error: File not found!!\n";
            return;
        }
        if (target->isDirectory)
        {
            cout << "Cannot shred a directory. Use rm instead.\n";
            return;
        }
        target->content.assign(target->content.length(), '#');
        cout << "Memory Shredded successfully for: " << target->name << "\n";
        target->parent->children.erase(target->name);
        delete target;
    }

    void findHelper(FileNode *currentNode, string targetName, string currentPath)
    {
        if (currentNode->name == targetName)
        {
            cout << "Found: " << currentPath << endl;
        }

        for (auto const &i : currentNode->children)
        {
            FileNode *childNode = i.second;
            string nextPath;
            if (currentPath == "/")
            {
                nextPath = currentPath + childNode->name;
            }
            else
            {
                nextPath = currentPath + "/" + childNode->name;
            }

            if (childNode->isDirectory)
            {
                findHelper(childNode, targetName, nextPath);
            }
            else if (childNode->name == targetName)
            {
                cout << "Found: " << nextPath << endl;
            }
        }
    }

    // find Command 3)
    void find(string targetName)
    {
        cout << "Searching Entire System For: " << targetName << "\n";

        findHelper(root, targetName, "/");
        cout << "Search Complete." << endl;
    }

    int duHelper(FileNode *currentNode)
    {
        int totalSize = 0;
        for (auto const &i : currentNode->children)
        {
            FileNode *childNode = i.second;
            if (childNode->isDirectory)
            {
                totalSize += duHelper(childNode);
            }
            else
            {
                totalSize += childNode->content.length();
            }
        }

        return totalSize;
    }

    void du(string targetPath)
    {
        FileNode *target = resolvePath(targetPath);
        if (target == nullptr)
        {
            cout << "Error: Directory not found !!\n";
            return;
        }
        if (target->isDirectory)
        {
            int finalSize = duHelper(target);
            cout << finalSize << " Bytes " << targetPath << "\n";
        }
        else
        {
            cout << target->content.length() << " Bytes " << targetPath << "\n";
        }
    }

    void showThemes()
    {
        cout << "\n"
             << currentTheme.accentColor << "Available Themes:" << currentTheme.reset << "\n";
        for (auto const &p : themes)
        {
            Theme t = p.second;
            cout << "  - " << t.accentColor << t.name << t.reset;
            cout << "  [" << t.dirColor << "DIR" << t.reset << " | " << t.fileColor << "FILE" << t.reset << "]";
            if (t.name == currentTheme.name)
            {
                cout << " (active)";
            }
            cout << "\n";
        }
        cout << "\nUsage: theme <name>\n\n";
    }

    void setTheme(const string &themeName)
    {
        if (!themes.contains(themeName))
        {
            cout << "Theme '" << themeName << "' not found.\n";
            showThemes();
            return;
        }
        currentTheme = themes[themeName];
        cout << currentTheme.accentColor << "Theme switched to: " << currentTheme.name << currentTheme.reset << "\n";
    }

    void props(string targetPath = ".")
    {
        FileNode *target = resolvePath(targetPath);
        if (target == nullptr)
        {
            cout << "props: cannot find '" << targetPath << "'\n";
            return;
        }

        string fullPath = "";
        pathFinder(target, fullPath);

        string border = "+--------------------------------------------------+";
        cout << "\n"
             << currentTheme.accentColor << border << currentTheme.reset << "\n";
        cout << currentTheme.accentColor << "| PROPERTIES INSPECTOR" << currentTheme.reset << "\n";
        cout << currentTheme.accentColor << border << currentTheme.reset << "\n";

        string nameColor;
        string typeLabel;
        if (target->isDirectory)
        {
            nameColor = currentTheme.dirColor;
            typeLabel = "Directory";
        }
        else
        {
            nameColor = currentTheme.fileColor;
            typeLabel = "File";
        }

        cout << "| " << left << setw(15) << "Name" << ": " << nameColor << target->name << currentTheme.reset << "\n";
        cout << "| " << left << setw(15) << "Type" << ": " << typeLabel << "\n";
        cout << "| " << left << setw(15) << "Full Path" << ": " << fullPath << "\n";

        if (target->isDirectory)
        {
            int totalBytes = duHelper(target);
            int subDirs = 0;
            int subFiles = 0;
            countSubtreeItems(target, subDirs, subFiles);

            cout << "| " << left << setw(15) << "Total Size" << ": " << totalBytes << " Bytes\n";
            cout << "| " << left << setw(15) << "Direct Items" << ": " << target->children.size() << "\n";
            cout << "| " << left << setw(15) << "Contains" << ": " << subDirs << " Folders, " << subFiles << " Files\n";
        }
        else
        {
            int lines = 0;
            int words = 0;
            countLinesAndWords(target->content, lines, words);

            cout << "| " << left << setw(15) << "Size" << ": " << target->content.length() << " Bytes\n";
            cout << "| " << left << setw(15) << "Lines" << ": " << lines << "\n";
            cout << "| " << left << setw(15) << "Words" << ": " << words << "\n";
        }

        cout << currentTheme.accentColor << border << currentTheme.reset << "\n\n";
    }

    void run()
    {
        load();
        string input;
        while (true)
        {
            string currentPath = "";
            pathFinder(currentDir, currentPath);
            string prompt = currentTheme.promptColor + "user@nanofs:" + currentTheme.dirColor + currentPath + currentTheme.reset + "$ ";
            input = readInputWithTab(prompt);

            if (input.empty())
                continue;

            commandHistory.push_back(input);
            if (commandHistory.size() > 50)
            {
                commandHistory.pop_front();
            }
            if (input == "cd")
            {
                cd("/");
                continue;
            }

            if (input == "exit")
            {
                save();
                break;
            }
            if (input == "history")
            {
                history();
                continue;
            }
            if (input == "clear")
            {
                clearScreen();
                continue;
            }
            if (input == "ls")
            {
                ls();
                continue;
            }
            if (input == "pwd")
            {
                pwd();
                continue;
            }
            if (input == "tree")
            {
                tree();
                continue;
            }
            if (input == "nanofetch")
            {
                nanofetch();
                continue;
            }
            if (input == "theme")
            {
                showThemes();
                continue;
            }
            if (input == "props")
            {
                props(".");
                continue;
            }
            if (input == "save")
            {
                save();
                continue;
            }
            if (input == "load")
            {
                if (load())
                {
                    cout << "Loaded Successfully\n";
                }
                continue;
            }

            size_t spacePos = input.find(' ');
            if (spacePos != string::npos)
            {
                string cmd = input.substr(0, spacePos);
                string arg = input.substr(spacePos + 1);

                if (cmd == "mkdir")
                    mkdir(arg);
                else if (cmd == "touch")
                    touch(arg);
                else if (cmd == "cd")
                {
                    if (arg.find_first_not_of(' ') == string::npos)
                    {
                        cd("/");
                    }
                    else
                    {
                        cd(arg);
                    }
                }
                else if (cmd == "rm")
                    rm(arg);
                else if (cmd == "cat")
                    cat(arg);
                else if (cmd == "empty")
                    empty(arg);
                else if (cmd == "theme")
                    setTheme(arg);
                else if (cmd == "write")
                {
                    size_t secondSpace = arg.find(' ');
                    if (secondSpace != string::npos)
                    {
                        write(arg.substr(0, secondSpace), arg.substr(secondSpace + 1));
                    }
                    else
                    {
                        cout << "Usage: write <filename> <text>\n";
                    }
                }
                else if (cmd == "trash")
                    trash(arg);
                else if (cmd == "restore")
                    restore(arg);
                else if (cmd == "ls")
                {
                    if (arg.find('*') != string::npos)
                    {
                        lsWildcard(arg);
                    }
                    else
                    {
                        ls(arg);
                    }
                }
                else if (cmd == "grep")
                    grep(arg);
                else if (cmd == "cp")
                    cp(arg);
                else if (cmd == "mv")
                    mv(arg);
                else if (cmd == "suggest")
                    suggest(arg);
                else if (cmd == "shred")
                    shred(arg);
                else if (cmd == "find")
                    find(arg);
                else if (cmd == "props")
                    props(arg);
                else if (cmd == "du")
                    du(arg);
                else
                    cout << "Command not recognized.\n";
            }
            else if (!input.empty())
            {
                cout << "Command not recognized.\n";
            }
        }
    }
};

int main()
{
    bootSequence();
    FileSystem fs;
    fs.run();
    return 0;
}
