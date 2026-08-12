#include "parse_ttstream.h"

#include "common.h"

TTStream::TTStream(Span parent, ParseState ps, const TokenTree& inputTt)
    : TokenStream(ps)
    , parentSpan(mv$(parent))
{
    DEBUG("parent " << parentSpan);
    for (auto s = parentSpan; s; s = s->parentSpan) {
        DEBUG("parent " << s->parentSpan);
    }
    DEBUG("input_tt = [" << inputTt << "]");
    DEBUG("Set edition " << inputTt.getEdition());
    edition = inputTt.getEdition();
    stack.push_back(::std::make_pair(0, &inputTt));
}

TTStream::~TTStream() {
}

Token TTStream::realGetToken() {
    while (stack.size() > 0) {
        // If current index is above TT size, go up
        unsigned int& idx = stack.back().first;
        assert(stack.back().second);
        const TokenTree& tree = *stack.back().second;

        if (idx == 0 && tree.isToken()) {
            idx++;
            hygienePtr = &tree.hygiene();
            DEBUG(tree.tok());
            return tree.tok();
        }

        if (idx < tree.size()) {
            const TokenTree& subtree = tree[idx];
            idx++;
            if (subtree.size() == 0) {
                hygienePtr = &subtree.hygiene();
                DEBUG(subtree.tok());
                return subtree.tok().clone();
            } else {
                stack.push_back(::std::make_pair(0, &subtree));
                DEBUG("Set edition " << edition << " -> " << subtree.getEdition());
                edition = subtree.getEdition();
            }
        } else {
            stack.pop_back();
            if (!stack.empty()) {
                DEBUG("Restore edition " << edition << " -> " << stack.back().second->getEdition());
                edition = stack.back().second->getEdition();
            }
        }
    }
    return Token(TOK_EOF);
}

Position TTStream::getPosition() const {
    // TODO: Position associated with the previous/next token?
    return Position(RcString(), 0, 0);
}

ASTEdition TTStream::realGetEdition() const {
    return edition;
}

Ident::Hygiene TTStream::realGetHygiene() const {
    // Empty.
    if (!hygienePtr) {
        return Ident::Hygiene();
    }
    return *hygienePtr;
}

TTStreamO::TTStreamO(Span parent, ParseState ps, TokenTree inputTt)
    : TokenStream(ps)
    , parentSpan(mv$(parent))
    , inputTt(mv$(inputTt))
{
    assert(parentSpan);
    stack.push_back(::std::make_pair(0, nullptr));
}

TTStreamO::~TTStreamO() {
}

Token TTStreamO::realGetToken() {
    while (stack.size() > 0) {
        // If current index is above TT size, go up
        unsigned int& idx = stack.back().first;
        TokenTree& tree = (stack.back().second ? *stack.back().second : inputTt);

        if (idx == 0 && tree.isToken()) {
            idx++;
            lastPos = tree.tok().getPos();
            edition = tree.getEdition();
            hygienePtr = &tree.hygiene();
            return mv$(tree.tok());
        }

        if (idx < tree.size()) {
            TokenTree& subtree = tree[idx];
            idx++;
            if (subtree.size() == 0) {
                lastPos = subtree.tok().getPos();
                edition = subtree.getEdition();
                hygienePtr = &subtree.hygiene();
                return mv$(subtree.tok());
            } else {
                stack.push_back(::std::make_pair(0, &subtree));
            }
        } else {
            stack.pop_back();
        }
    }
    return Token(TOK_EOF);
}

ASTEdition TTStreamO::realGetEdition() const {
    return edition;
}

Position TTStreamO::getPosition() const {
    return lastPos;
}

Ident::Hygiene TTStreamO::realGetHygiene() const {
    // Empty.
    if (!hygienePtr) {
        return Ident::Hygiene();
    }
    return *hygienePtr;
}
