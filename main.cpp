/*
    Mini Library Catalog (C++17, single-file)
    Features:
      - Add / edit / delete books
      - List with pagination
      - Search (case-insensitive substring)
      - Filter by year range and by read-status
      - Sort by title/author/year/rating
      - Save/Load CSV (simple escaping)
      - Export pretty report to txt

    Build:
      g++ -std=c++17 -O2 -Wall -Wextra library.cpp -o library

    Note:
      CSV escaping is minimal but sufficient for commas/quotes/newlines.
*/

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using std::cin;
using std::cout;
using std::string;

static void clearInputLine() {
    cin.clear();
    cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

static string trim(const string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace((unsigned char)s[b])) b++;
    size_t e = s.size();
    while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

static string toLower(string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool icontains(const string& hay, const string& needle) {
    if (needle.empty()) return true;
    string h = toLower(hay);
    string n = toLower(needle);
    return h.find(n) != string::npos;
}

static int readInt(const string& prompt, int fallback, int minVal, int maxVal) {
    for (;;) {
        cout << prompt;
        string line;
        if (!std::getline(cin, line)) return fallback;
        line = trim(line);
        if (line.empty()) return fallback;

        try {
            long long v = std::stoll(line);
            if (v < minVal || v > maxVal) {
                cout << "Value out of range (" << minVal << ".." << maxVal << ").\n";
                continue;
            }
            return (int)v;
        } catch (...) {
            cout << "Invalid number.\n";
        }
    }
}

static double readDouble(const string& prompt, double fallback, double minVal, double maxVal) {
    for (;;) {
        cout << prompt;
        string line;
        if (!std::getline(cin, line)) return fallback;
        line = trim(line);
        if (line.empty()) return fallback;

        try {
            double v = std::stod(line);
            if (v < minVal || v > maxVal) {
                cout << "Value out of range (" << minVal << ".." << maxVal << ").\n";
                continue;
            }
            return v;
        } catch (...) {
            cout << "Invalid number.\n";
        }
    }
}

static bool readYesNo(const string& prompt, bool fallback) {
    for (;;) {
        cout << prompt;
        string line;
        if (!std::getline(cin, line)) return fallback;
        line = toLower(trim(line));
        if (line.empty()) return fallback;
        if (line == "y" || line == "yes" || line == "да" || line == "д") return true;
        if (line == "n" || line == "no"  || line == "нет" || line == "н") return false;
        cout << "Enter yes/no (y/n).\n";
    }
}

/* --------------------------- CSV helpers --------------------------- */

static string csvEscape(const string& field) {
    bool needQuotes = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needQuotes = true;
            break;
        }
    }
    if (!needQuotes) return field;

    string out;
    out.reserve(field.size() + 2);
    out.push_back('"');
    for (char c : field) {
        if (c == '"') out.push_back('"'); // double quote
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

static std::vector<string> csvParseLine(const string& line) {
    std::vector<string> fields;
    string cur;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur.push_back('"'); // escaped quote
                    i++;
                } else {
                    inQuotes = false; // end quotes
                }
            } else {
                cur.push_back(c);
            }
        } else {
            if (c == ',') {
                fields.push_back(cur);
                cur.clear();
            } else if (c == '"') {
                inQuotes = true;
            } else {
                cur.push_back(c);
            }
        }
    }
    fields.push_back(cur);
    return fields;
}

/* --------------------------- Domain model --------------------------- */

struct Book {
    int id = 0;
    string title;
    string author;
    int year = 0;
    string isbn;
    double rating = 0.0; // 0..5
    bool read = false;

    string toCsv() const {
        std::ostringstream oss;
        oss  << id << ","
             << csvEscape(title) << ","
             << csvEscape(author) << ","
             << year << ","
             << csvEscape(isbn) << ","
             << std::fixed << std::setprecision(2) << rating << ","
             << (read ? 1 : 0);
        return oss.str();
    }

    static std::optional<Book> fromCsv(const string& line) {
        auto f = csvParseLine(line);
        if (f.size() != 7) return std::nullopt;

        Book b;
        try {
            b.id = std::stoi(trim(f[0]));
            b.title = f[1];
            b.author = f[2];
            b.year = std::stoi(trim(f[3]));
            b.isbn = f[4];
            b.rating = std::stod(trim(f[5]));
            b.read = (std::stoi(trim(f[6])) != 0);
        } catch (...) {
            return std::nullopt;
        }
        return b;
    }
};

class Library {
public:
    void add(Book b) {
        if (b.id == 0) b.id = nextId_++;
        else nextId_ = std::max(nextId_, b.id + 1);
        books_.push_back(std::move(b));
    }

    bool removeById(int id) {
        auto it = std::remove_if(books_.begin(), books_.end(),
                                 [&](const Book& b){ return b.id == id; });
        if (it == books_.end()) return false;
        books_.erase(it, books_.end());
        return true;
    }

    Book* findById(int id) {
        for (auto& b : books_) if (b.id == id) return &b;
        return nullptr;
    }

    const std::vector<Book>& all() const { return books_; }

    std::vector<Book*> search(const string& q) {
        std::vector<Book*> out;
        for (auto& b : books_) {
            if (icontains(b.title, q) || icontains(b.author, q) || icontains(b.isbn, q)) {
                out.push_back(&b);
            }
        }
        return out;
    }

    std::vector<Book*> filter(std::optional<int> yearFrom,
                              std::optional<int> yearTo,
                              std::optional<bool> readStatus) {
        std::vector<Book*> out;
        for (auto& b : books_) {
            if (yearFrom && b.year < *yearFrom) continue;
            if (yearTo && b.year > *yearTo) continue;
            if (readStatus && b.read != *readStatus) continue;
            out.push_back(&b);
        }
        return out;
    }

    enum class SortKey { Title, Author, Year, Rating, Id };

    void sortBy(SortKey key, bool ascending) {
        auto cmp = [&](const Book& a, const Book& b) {
            auto less = [&](auto&& x, auto&& y) { return ascending ? x < y : x > y; };
            switch (key) {
                case SortKey::Title:  return less(toLower(a.title),  toLower(b.title));
                case SortKey::Author: return less(toLower(a.author), toLower(b.author));
                case SortKey::Year:   return less(a.year, b.year);
                case SortKey::Rating: return less(a.rating, b.rating);
                case SortKey::Id:     return less(a.id, b.id);
            }
            return false;
        };
        std::sort(books_.begin(), books_.end(), cmp);
    }

    bool saveCsv(const string& path) const {
        std::ofstream out(path);
        if (!out) return false;
        out << "id,title,author,year,isbn,rating,read\n";
        for (const auto& b : books_) out << b.toCsv() << "\n";
        return true;
    }

    bool loadCsv(const string& path) {
        std::ifstream in(path);
        if (!in) return false;

        books_.clear();
        nextId_ = 1;

        string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first) { first = false; continue; } // skip header
            if (trim(line).empty()) continue;

            auto ob = Book::fromCsv(line);
            if (ob) add(*ob);
        }
        return true;
    }

    bool exportReportTxt(const string& path) const {
        std::ofstream out(path);
        if (!out) return false;

        out << "LIBRARY REPORT\n";
        out << "Books: " << books_.size() << "\n";
        out << "----------------------------------------\n";

        // some stats
        int readCount = 0;
        double avgRating = 0.0;
        int ratedCount = 0;
        int minYear = 999999, maxYear = -999999;

        for (const auto& b : books_) {
            readCount += (b.read ? 1 : 0);
            if (b.rating > 0.0) { avgRating += b.rating; ratedCount++; }
            minYear = std::min(minYear, b.year);
            maxYear = std::max(maxYear, b.year);
        }
        if (ratedCount > 0) avgRating /= ratedCount;

        out << "Read: " << readCount << " / " << books_.size() << "\n";
        out << "Avg rating (rated only): " << std::fixed << std::setprecision(2) << avgRating << "\n";
        if (!books_.empty()) out << "Year range: " << minYear << " .. " << maxYear << "\n";
        out << "----------------------------------------\n\n";

        for (const auto& b : books_) {
            out << "#" << b.id << " [" << (b.read ? "READ" : "TODO") << "] "
                << b.title << " — " << b.author
                << " (" << b.year << ")"
                << " rating=" << std::fixed << std::setprecision(2) << b.rating
                << "\n";
            if (!b.isbn.empty()) out << "    ISBN: " << b.isbn << "\n";
            out << "\n";
        }
        return true;
    }

private:
    std::vector<Book> books_;
    int nextId_ = 1;
};

/* --------------------------- UI helpers --------------------------- */

static void printBookRow(const Book& b) {
    cout << std::setw(4) << b.id << "  "
         << (b.read ? "[x] " : "[ ] ")
         << std::left << std::setw(28) << (b.title.size() > 28 ? b.title.substr(0,25)+"..." : b.title)
         << std::left << std::setw(20) << (b.author.size()>20? b.author.substr(0,17)+"..." : b.author)
         << std::right << std::setw(6) << b.year << "  "
         << "★" << std::fixed << std::setprecision(2) << std::setw(4) << b.rating
         << "  "
         << (b.isbn.size()>14? b.isbn.substr(0,11)+"..." : b.isbn)
         << "\n";
    cout << std::right; // restore
}

static void listPaged(const std::vector<Book*>& items, int pageSize) {
    if (items.empty()) {
        cout << "No books.\n";
        return;
    }

    int total = (int)items.size();
    int page = 0;
    int pages = (total + pageSize - 1) / pageSize;

    for (;;) {
        int start = page * pageSize;
        int end = std::min(start + pageSize, total);

        cout << "\n--- Page " << (page + 1) << "/" << pages
             << " (" << total << " books) ---\n";
        cout << " ID   R   TITLE                        AUTHOR               YEAR  RATE  ISBN\n";
        cout << "-------------------------------------------------------------------------------\n";

        for (int i = start; i < end; i++) {
            printBookRow(*items[i]);
        }

        cout << "-------------------------------------------------------------------------------\n";
        cout << "[n]ext, [p]rev, [q]uit: ";
        string cmd;
        std::getline(cin, cmd);
        cmd = toLower(trim(cmd));

        if (cmd == "q" || cmd == "quit") break;
        if (cmd == "n" || cmd == "next") {
            if (page + 1 < pages) page++;
            else cout << "Already last page.\n";
        } else if (cmd == "p" || cmd == "prev") {
            if (page > 0) page--;
            else cout << "Already first page.\n";
        } else {
            cout << "Unknown.\n";
        }
    }
}

static Book inputBookInteractive(int id = 0, const Book* existing = nullptr) {
    Book b;
    if (existing) b = *existing;
    b.id = id;

    cout << "Title" << (existing ? " (Enter keep)" : "") << ": ";
    string line;
    std::getline(cin, line);
    line = trim(line);
    if (!line.empty()) b.title = line;

    cout << "Author" << (existing ? " (Enter keep)" : "") << ": ";
    std::getline(cin, line);
    line = trim(line);
    if (!line.empty()) b.author = line;

    int currentYear = 2026; // ok as a default; can change
    b.year = readInt("Year (e.g., 1999): ", existing ? b.year : currentYear, 0, 3000);

    cout << "ISBN" << (existing ? " (Enter keep)" : " (optional)") << ": ";
    std::getline(cin, line);
    line = trim(line);
    if (!line.empty() || !existing) b.isbn = line;

    b.rating = readDouble("Rating 0..5 (Enter keep/default): ",
                          existing ? b.rating : 0.0, 0.0, 5.0);

    b.read = readYesNo(string("Read? (y/n, Enter keep/default): "),
                       existing ? b.read : false);

    if (b.title.empty()) b.title = "(untitled)";
    if (b.author.empty()) b.author = "(unknown)";
    return b;
}

/* --------------------------- Main menu --------------------------- */

static void printMenu() {
    cout << "\n=== Mini Library ===\n";
    cout << "1) List all\n";
    cout << "2) Add book\n";
    cout << "3) Edit book\n";
    cout << "4) Delete book\n";
    cout << "5) Search\n";
    cout << "6) Filter\n";
    cout << "7) Sort\n";
    cout << "8) Save CSV\n";
    cout << "9) Load CSV\n";
    cout << "10) Export report TXT\n";
    cout << "0) Exit\n";
}

int main() {
    std::ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Library lib;

    // seed a few examples
    lib.add(Book{0,"The Pragmatic Programmer","Andrew Hunt",1999,"978-0201616224",4.8,true});
    lib.add(Book{0,"Clean Code","Robert C. Martin",2008,"978-0132350884",4.6,false});
    lib.add(Book{0,"Design Patterns","GoF",1994,"978-0201633610",4.4,false});

    for (;;) {
        printMenu();
        int choice = readInt("Select: ", -1, 0, 10);

        if (choice == 0) {
            cout << "Bye!\n";
            break;
        }

        switch (choice) {
            case 1: { // List all
                std::vector<Book*> items;
                for (auto& b : const_cast<std::vector<Book>&>(*(const std::vector<Book>*)&lib.all())) {
                    // Trick: we need non-const pointers for listPaged; for demo only.
                    // Safer approach: store indices or copy. We'll do a clean approach below.
                }
                // Clean approach: build pointers by searching IDs (no const-cast of internal vector):
                for (const auto& cb : lib.all()) {
                    // We can't take non-const pointer from const ref. Instead, search by id.
                    Book* p = lib.findById(cb.id);
                    if (p) items.push_back(p);
                }
                listPaged(items, 8);
            } break;

            case 2: { // Add
                cout << "\n-- Add book --\n";
                Book b = inputBookInteractive(0, nullptr);
                lib.add(std::move(b));
                cout << "Added.\n";
            } break;

            case 3: { // Edit
                int id = readInt("Book id: ", -1, 1, 1000000000);
                Book* b = lib.findById(id);
                if (!b) { cout << "Not found.\n"; break; }
                cout << "\n-- Edit #" << id << " --\n";
                Book updated = inputBookInteractive(id, b);
                *b = std::move(updated);
                cout << "Updated.\n";
            } break;

            case 4: { // Delete
                int id = readInt("Delete id: ", -1, 1, 1000000000);
                if (lib.removeById(id)) cout << "Deleted.\n";
                else cout << "Not found.\n";
            } break;

            case 5: { // Search
                cout << "Query: ";
                string q;
                std::getline(cin, q);
                q = trim(q);
                auto items = lib.search(q);
                listPaged(items, 8);
            } break;

            case 6: { // Filter
                cout << "Year from (Enter skip): ";
                string line;
                std::getline(cin, line);
                line = trim(line);
                std::optional<int> yf;
                if (!line.empty()) {
                    try { yf = std::stoi(line); } catch (...) {}
                }

                cout << "Year to (Enter skip): ";
                std::getline(cin, line);
                line = trim(line);
                std::optional<int> yt;
                if (!line.empty()) {
                    try { yt = std::stoi(line); } catch (...) {}
                }

                cout << "Read status: [a]ll / [r]ead / [u]nread (Enter=all): ";
                std::getline(cin, line);
                line = toLower(trim(line));

                std::optional<bool> rs;
                if (line == "r" || line == "read") rs = true;
                else if (line == "u" || line == "unread") rs = false;

                auto items = lib.filter(yf, yt, rs);
                listPaged(items, 8);
            } break;

            case 7: { // Sort
                cout << "Sort by: 1=title, 2=author, 3=year, 4=rating, 5=id: ";
                string line;
                std::getline(cin, line);
                line = trim(line);
                int k = 1;
                if (!line.empty()) {
                    try { k = std::stoi(line); } catch (...) { k = 1; }
                }

                bool asc = readYesNo("Ascending? (y/n, Enter=y): ", true);

                Library::SortKey key = Library::SortKey::Title;
                if (k == 2) key = Library::SortKey::Author;
                else if (k == 3) key = Library::SortKey::Year;
                else if (k == 4) key = Library::SortKey::Rating;
                else if (k == 5) key = Library::SortKey::Id;

                lib.sortBy(key, asc);
                cout << "Sorted.\n";
            } break;

            case 8: { // Save CSV
                cout << "Path (Enter=library.csv): ";
                string path;
                std::getline(cin, path);
                path = trim(path);
                if (path.empty()) path = "library.csv";
                if (lib.saveCsv(path)) cout << "Saved to " << path << "\n";
                else cout << "Save failed.\n";
            } break;

            case 9: { // Load CSV
                cout << "Path (Enter=library.csv): ";
                string path;
                std::getline(cin, path);
                path = trim(path);
                if (path.empty()) path = "library.csv";
                if (lib.loadCsv(path)) cout << "Loaded from " << path << "\n";
                else cout << "Load failed.\n";
            } break;

            case 10: { // Export report
                cout << "Report path (Enter=report.txt): ";
                string path;
                std::getline(cin, path);
                path = trim(path);
                if (path.empty()) path = "report.txt";
                if (lib.exportReportTxt(path)) cout << "Exported " << path << "\n";
                else cout << "Export failed.\n";
            } break;

            default:
                cout << "Unknown.\n";
                break;
        }
    }

    return 0;
}
