#pragma once
#include <algorithm>
#include <cwctype>
#include "Common.hpp"

/** A whitespace-separated word: its line and codepoint range within that line. */
struct Token {
	int line, start, len;
};

/** The editable text: a list of lines plus a cursor. Always holds at least one line. */
struct Doc {
	std::vector<std::u32string> lines{U""};
	int row = 0, col = 0;

	int count() const { return int(lines.size()); }
	std::u32string& cur() { return lines[row]; }

	void clamp() {
		row = std::clamp(row, 0, count() - 1);
		col = std::clamp(col, 0, int(cur().size()));
	}

	void insert(const std::u32string& s) {
		for (char32_t c : s) {
			if (c == U'\n')
				newline();
			else if (c >= 0x20 && c != 0x7F)
				cur().insert(cur().begin() + col++, c);
		}
	}
	void newline() {
		lines.insert(lines.begin() + row + 1, cur().substr(col));
		cur().erase(col);
		row++, col = 0;
	}
	void backspace() {
		if (col > 0)
			cur().erase(--col, 1);
		else if (row > 0) {
			col = int(lines[row - 1].size());
			lines[row - 1] += cur();
			lines.erase(lines.begin() + row--);
		}
	}
	void del() {
		if (col < int(cur().size()))
			cur().erase(col, 1);
		else if (row + 1 < count()) {
			cur() += lines[row + 1];
			lines.erase(lines.begin() + row + 1);
		}
	}
	void moveCol(int d) {
		for (; d > 0; d--) {
			if (col < int(cur().size()))
				col++;
			else if (row + 1 < count())
				row++, col = 0;
		}
		for (; d < 0; d++) {
			if (col > 0)
				col--;
			else if (row > 0)
				row--, col = int(cur().size());
		}
	}
	void moveRow(int d) {
		row += d;
		clamp();
	}
	void home() { col = 0; }
	void end() { col = int(cur().size()); }

	void insertLine() {
		lines.insert(lines.begin() + row + 1, U"");
		row++, col = 0;
	}
	void deleteLine() {
		if (count() == 1)
			cur().clear();
		else
			lines.erase(lines.begin() + row);
		clamp();
	}
	void duplicateLine() {
		lines.insert(lines.begin() + row + 1, cur());
		row++;
	}
	void moveLine(int d) {
		int to = row + d;
		if (to < 0 || to >= count())
			return;
		std::swap(lines[row], lines[to]);
		row = to;
	}
	std::vector<Token> tokens() const {
		std::vector<Token> out;
		for (int l = 0; l < count(); l++) {
			const std::u32string& s = lines[l];
			for (int i = 0; i < int(s.size());) {
				while (i < int(s.size()) && std::iswspace(s[i]))
					i++;
				int start = i;
				while (i < int(s.size()) && !std::iswspace(s[i]))
					i++;
				if (i > start)
					out.push_back({l, start, i - start});
			}
		}
		return out;
	}
	/** The token under (or next after) the cursor, else the last one before it. */
	static int tokenAt(const std::vector<Token>& tokens, int row, int col) {
		int best = -1;
		for (int i = 0; i < int(tokens.size()); i++) {
			const Token& t = tokens[i];
			if (t.line < row || (t.line == row && t.start + t.len < col))
				best = i;
			else
				return t.line == row ? i : std::max(best, 0);
		}
		return best;
	}

	void setText(const std::string& utf8) {
		lines.assign(1, U"");
		row = col = 0;
		insert(toUtf32(utf8));
		row = col = 0;
	}
};
