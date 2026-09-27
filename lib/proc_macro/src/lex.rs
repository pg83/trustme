use crate::TokenStream;
use crate::{Ident,Literal};
use crate::{Punct,Spacing};
use crate::Span;

struct CharStream<T: Iterator<Item=char>> {
    inner: ::std::iter::Peekable<T>,
    cur: Option<char>,
}
impl<T: Iterator<Item=char>> CharStream<T> {
    fn new(i: T) -> Self {
        CharStream {
            inner: i.peekable(),
            cur: Some(' '),
            }
    }
    fn consume(&mut self) -> Option<char> {
        self.cur = self.inner.next();
        self.cur
    }
    fn is_complete(&self) -> bool {
        self.cur.is_none()
    }
    fn cur(&self) -> char {
        self.cur.expect("CharStream::cur called with no current")
    }
    fn next(&mut self) -> Option<char> {
        self.inner.peek().cloned()
    }
}

static SYMS: [&[u8]; 53] = [
    b"!" as &[u8],
    b"!=",
    b"#",
    b"$",
    b"%", b"%=",
    b"&", b"&&", b"&=",
    b"(",
    b")",
    b"*", b"*=",
    b"+", b"+=",
    b",",
    b"-", b"-=", b"->",
    b".", b"..", b"...",
    b"/", b"/=",
    b":", b"::",
    b";",
    b"<", b"<-", b"<<", b"<<=", b"<=",
    b"=", b"==", b"=>",
    b">", b">=", b">>", b">>=",
    b"?",
    b"@",
    b"[",
    b"\\",
    b"]",
    b"^", b"^=",
    b"`",
    b"{",
    b"|", b"|=", b"||",
    b"}",
    b"~",
    ];

pub struct LexError {
    pub(crate) inner: &'static str,
}
impl ::std::fmt::Display for LexError {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        f.write_str(self.inner)
    }
}
impl ::std::fmt::Debug for LexError {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "LexError({})", self.inner)
    }
}

fn group_delimiters(tokens: Vec<crate::TokenTree>) -> Result<Vec<crate::TokenTree>, LexError> {
    let mut groups: Vec<(char, Vec<crate::TokenTree>)> = Vec::new();
    let mut current = Vec::new();

    for token in tokens {
        match token {
        crate::TokenTree::Punct(ref punct) if punct.as_char() == '(' || punct.as_char() == '[' || punct.as_char() == '{' => {
            groups.push((punct.as_char(), current));
            current = Vec::new();
            },
        crate::TokenTree::Punct(ref punct) if punct.as_char() == ')' || punct.as_char() == ']' || punct.as_char() == '}' => {
            let (open, mut parent) = match groups.pop() {
                Some(group) => group,
                None => return Err(LexError { inner: "Unexpected closing delimiter" }),
                };
            let delimiter = match (open, punct.as_char()) {
                ('(', ')') => crate::Delimiter::Parenthesis,
                ('[', ']') => crate::Delimiter::Bracket,
                ('{', '}') => crate::Delimiter::Brace,
                _ => return Err(LexError { inner: "Mismatched closing delimiter" }),
                };
            parent.push(crate::Group::new(delimiter, TokenStream::from_trees(current)).into());
            current = parent;
            },
        token => current.push(token),
        }
    }

    if groups.is_empty() {
        Ok(current)
    }
    else {
        Err(LexError { inner: "Unclosed delimiter" })
    }
}

impl ::std::str::FromStr for TokenStream {
    type Err = LexError;
    fn from_str(src: &str) -> Result<TokenStream, LexError> {
        debug!("TokenStream::from_str({:?})\r", src);
        let mut rv: Vec<crate::TokenTree> = Vec::new();
        let mut it = CharStream::new(src.chars());

        fn err(s: &'static str) -> Result<TokenStream,LexError> {
            Err(LexError { inner: s })
        }

        fn spelled(text: &str) -> Result<Literal, LexError> {
            Literal::from_spelling(text, crate::Span::call_site()).ok_or(LexError { inner: "Malformed literal" })
        }

        fn get_ident<T: Iterator<Item=char>>(it: &mut CharStream<T>, mut s: String) -> String
        {
            let mut c = it.cur();
            while c == '_' || c.is_alphanumeric() || c.is_digit(10)
            {
                s.push(c);
                c = some_else!(it.consume() => break);
            }
            s
        }

        fn starts_punctuation<T: Iterator<Item=char>>(it: &CharStream<T>, c: char) -> bool
        {
            if c == '/' && (it.next() == Some('/') || it.next() == Some('*')) {
                return false;
            }
            match c {
            '~' | '!' | '@' | '#' | '$' | '%' | '^' | '&' | '*' | '-' | '=' | '+' |
            '|' | ';' | ':' | ',' | '<' | '.' | '>' | '/' | '?' | '\'' => true,
            _ => false,
            }
        }

        'outer: while ! it.is_complete()
        {
            let mut c = it.cur();

            if c.is_whitespace() {
                it.consume();
                continue ;
            }

            if c == '\''
            {
                c = match it.consume() {
                    Some(c) => c,
                    None => return err("Unterminated char literal"),
                    };
                if (c.is_alphabetic() || c == '_') && it.next().map(|x| x != '\'').unwrap_or(true) {
                    // Lifetime
                    let ident = get_ident(&mut it, String::new());
                    rv.push(Punct::from_char('\'', Spacing::Joint, crate::Span::call_site()).into());
                    rv.push(Ident::from_name(&ident, false, crate::Span::call_site()).into());
                }
                else {
                    // Char lit, kept as written between its quotes
                    let mut spelling = String::from("'");
                    spelling.push(c);
                    if c == '\\' {
                        let escaped = some_else!(it.consume() => return err("Unterminated char literal"));
                        spelling.push(escaped);
                        match escaped
                        {
                        '0' | 'n' | 'r' | 't' | '\\' | '\'' | '"' => {},
                        'u' => {
                            loop {
                                let d = some_else!(it.consume() => return err("Unterminated `\\u` escape"));
                                spelling.push(d);
                                if d == '}' {
                                    break;
                                }
                            }
                            },
                        'x' => {
                            for _ in 0 .. 2 {
                                let d = some_else!(it.consume() => return err("Unterminated `\\x` escape"));
                                if d.to_digit(16).is_none() {
                                    return err("Invalid hex digit in `\\x` escape");
                                }
                                spelling.push(d);
                            }
                            },
                        _ => return err("Unknown escape in char literal"),
                        }
                    }
                    match it.consume()
                    {
                    Some('\'') => spelling.push('\''),
                    Some(c) => {
                        debug!("Stray charcter '{}'", c);
                        return err("Multiple characters in char literal");
                        },
                    None => {
                        return err("Unterminated char literal");
                        },
                    }
                    rv.push(spelled(&spelling)?.into());
                    it.consume();   // Eat the final `'` returned above
                }
            }
            else if c == '/' && it.next() == Some('/')
            {
                // Line comment
                while it.consume() != Some('\n') {
                }
            }
            else if c == '/' && it.next() == Some('*')
            {
                // Block comment
                let mut level: u32 = 1;
                it.consume();
                loop {
                    match it.consume()
                    {
                    Some('*') => {
                        if it.next() == Some('/') {
                            it.consume();
                            it.consume();
                            level -= 1;
                            if level == 0 {
                                break;
                            }
                        }
                        },
                    Some('/') => {
                        if it.next() == Some('*') {
                            it.consume();
                            it.consume();
                            level += 1;
                        }
                        },
                    None => panic!("Unexpected EOF in block comment"),
                    _ => {},
                    }
                }
            }
            else
            {
                // byte or raw string literals
                if c == 'b' || c == 'r' || c == '"' {
                    let mut c = c;
                    let is_byte = if c == 'b' {
                            c = some_else!(it.consume() => { rv.push(Ident::new("b", crate::Span::call_site()).into()); break });
                            true
                        } else {
                            false
                        };

                    if c == 'r'
                    {
                        // TODO: If this isn't a string, start parsing an ident instead.
                        let ident_str = if is_byte { "br" } else { "r" };
                        c = some_else!(it.consume() => { rv.push(Ident::new(ident_str.into(), crate::Span::call_site()).into()); break });
                        let mut hashes = 0;
                        while c == '#' {
                            hashes += 1;
                            c = some_else!(it.consume() => return err("rawstr eof"));
                        }

                        if c != '"' {
                            if hashes == 0 {
                                let s = get_ident(&mut it, ident_str.to_string());
                                rv.push(Ident::from_name(&s, false, crate::Span::call_site()).into());
                            }
                            else {
                                rv.push(Ident::new(ident_str.into(), crate::Span::call_site()).into());
                            }
                            while hashes > 0 {
                                rv.push(Punct::from_char('#', if hashes == 1 { Spacing::Alone } else { Spacing::Joint }, crate::Span::call_site()).into());
                                hashes -= 1;
                            }
                            continue 'outer;
                        }

                        let req_hashes = hashes;
                        let mut rawstr = String::new();
                        loop
                        {
                            c = some_else!(it.consume() => return err("Rawstr eof"));
                            if c == '"' {
                                let mut hashes = 0;
                                while hashes < req_hashes {
                                    c = some_else!(it.consume() => return err("rawstr eof"));
                                    if c != '#' { break ; }
                                    hashes += 1;
                                }

                                if hashes != req_hashes {
                                    rawstr.push('"');
                                    for _ in 0 .. hashes {
                                        rawstr.push('#');
                                    }
                                    rawstr.push(c);
                                }
                                else {
                                    break ;
                                }
                            }
                            else {
                                rawstr.push(c);
                            }
                        }

                        it.consume();
                        let mut spelling = ident_str.to_string();
                        for _ in 0 .. req_hashes {
                            spelling.push('#');
                        }
                        spelling.push('"');
                        spelling.push_str(&rawstr);
                        spelling.push('"');
                        for _ in 0 .. req_hashes {
                            spelling.push('#');
                        }
                        rv.push(spelled(&spelling)?.into());
                        continue 'outer;
                    }
                    else if c == '\''
                    {
                        // Byte character literal?
                        // NOTE: That b'foo is not `b` followed by `'foo`
                        assert!(is_byte);
                        let mut spelling = String::from("b'");
                        c = some_else!(it.consume() => return err("Unterminated byte character literal"));
                        if c == '\\' {
                            spelling.push(c);
                            c = some_else!(it.consume() => return err("Unterminated byte character literal"));
                            spelling.push(c);
                            match c {
                            '0' | 'n' | 'r' | 't' | '\\' | '\'' | '"' => {},
                            'x' => {
                                for _ in 0 .. 2 {
                                    c = some_else!(it.consume() => return err("Unterminated byte character literal"));
                                    if c.to_digit(16).is_none() {
                                        return err("Invalid hex digit in byte character literal");
                                    }
                                    spelling.push(c);
                                }
                                },
                            _ => return err("Invalid escape in byte character literal"),
                            }
                        }
                        else if c as u32 <= 0x7f && c != '\n' && c != '\r' {
                            spelling.push(c);
                        }
                        else {
                            return err("Invalid byte character literal");
                        }

                        match it.consume() {
                        Some('\'') => spelling.push('\''),
                        Some(_) => return err("Multiple characters in byte character literal"),
                        None => return err("Unterminated byte character literal"),
                        }
                        it.consume();
                        rv.push(spelled(&spelling)?.into());
                        continue 'outer;
                    }
                    else if c == '\"'
                    {
                        // String literal, kept as written between its quotes
                        let mut spelling = String::from(if is_byte { "b\"" } else { "\"" });
                        loop
                        {
                            c = some_else!(it.consume() => return err("str eof"));
                            spelling.push(c);
                            if c == '"' {
                                it.consume();
                                break ;
                            }
                            else if c == '\\' {
                                let escaped = some_else!(it.consume() => return err("str eof"));
                                spelling.push(escaped);
                                match escaped
                                {
                                'n' | 'r' | 't' | '0' | '\\' | '\'' | '"' => {},
                                'x' => {
                                    for _ in 0 .. 2 {
                                        let d = some_else!(it.consume() => return err("str eof"));
                                        if d.to_digit(16).is_none() {
                                            return err("Invalid hex digit in `\\x` escape");
                                        }
                                        spelling.push(d);
                                    }
                                    },
                                'u' => {
                                    loop {
                                        let d = some_else!(it.consume() => return err("Unterminated `\\u` escape"));
                                        spelling.push(d);
                                        if d == '}' {
                                            break;
                                        }
                                    }
                                    },
                                // A backslash at end-of-line eats the newline and the next line's leading whitespace
                                '\n' | '\r' => {
                                    while it.next().map(|c| c.is_whitespace()).unwrap_or(false) {
                                        spelling.push(some_else!(it.consume() => return err("str eof")));
                                    }
                                    },
                                _ => return err("Unknown escape in string"),
                                }
                            }
                        }
                        rv.push(spelled(&spelling)?.into());
                        continue 'outer;
                    }
                    else
                    {
                        // Could be an ident starting with 'b', or it's just 'b'
                        // - Fall through
                        let ident = get_ident(&mut it, "b".into());
                        rv.push(Ident::from_name(&ident, false, crate::Span::call_site()).into());
                        continue 'outer;
                    }
                }

                // Identifier.
                if c.is_alphabetic() || c == '_'
                {
                    let ident = get_ident(&mut it, String::new());
                    if false && ident == "_" {
                        rv.push(Punct::from_char('_', Spacing::Alone, crate::Span::call_site()).into());
                    }
                    else {
                        rv.push(Ident::from_name(&ident, false, crate::Span::call_site()).into());
                    }
                }
                else if c.is_digit(10)
                {
                    let leading_zero = c == '0';
                    let mut number_spelling = String::new();
                    let base =
                        if leading_zero {
                            number_spelling.push('0');
                            match it.consume()
                            {
                            Some('x') => { number_spelling.push('x'); it.consume(); 16 },
                            Some('o') => { number_spelling.push('o'); it.consume(); 8 },
                            Some('b') => { number_spelling.push('b'); it.consume(); 2 },
                            None => {
                                rv.push(spelled(&number_spelling)?.into());
                                continue 'outer;
                                },
                            _ => 10,
                            }
                        }
                        else {
                            10
                        };
                    let mut c = it.cur();
                    'int: loop
                    {
                        while c == '_' {
                            number_spelling.push(c);
                            c = some_else!( it.consume() => { break 'int; } );
                        }
                        if c == 'u' || c == 'i' {
                            let s = get_ident(&mut it, String::new());
                            match &*s {
                                "u8" | "i8" | "u16" | "i16" | "u32" | "i32" |
                                "u64" | "i64" | "u128" | "i128" | "usize" | "isize" => {},
                                _ => return err("Unexpected integer suffix"),
                            }
                            number_spelling.push_str(&s);
                            rv.push(spelled(&number_spelling)?.into());
                            continue 'outer;
                        }
                        else if c.to_digit(base).is_some() {
                            number_spelling.push(c);
                            c = some_else!( it.consume() => { break 'int; } );
                        }
                        else if base == 10 && (c == '.' || c == 'e' || c == 'E' || c == 'f') {
                            if c == '.' && it.next().map(|next| {
                                next == '.' || next == '_' || next.is_alphabetic()
                            }).unwrap_or(false) {
                                break;
                            }

                            let mut current = Some(c);
                            if current == Some('.') {
                                number_spelling.push('.');
                                current = it.consume();
                                loop {
                                    match current {
                                    Some(ch) if ch == '_' || ch.is_digit(10) => {
                                        number_spelling.push(ch);
                                        current = it.consume();
                                        },
                                    _ => break,
                                    }
                                }
                            }

                            if current == Some('e') || current == Some('E') {
                                number_spelling.push(current.unwrap());
                                current = it.consume();
                                if current == Some('+') || current == Some('-') {
                                    number_spelling.push(current.unwrap());
                                    current = it.consume();
                                }

                                let mut has_exponent_digit = false;
                                loop {
                                    match current {
                                    Some('_') => {
                                        number_spelling.push('_');
                                        current = it.consume();
                                        },
                                    Some(ch) if ch.is_digit(10) => {
                                        has_exponent_digit = true;
                                        number_spelling.push(ch);
                                        current = it.consume();
                                        },
                                    _ => break,
                                    }
                                }
                                if !has_exponent_digit {
                                    return err("Missing digits in float exponent");
                                }
                            }

                            if current == Some('f') {
                                let suffix = get_ident(&mut it, String::new());
                                match &*suffix {
                                "f32" | "f64" => number_spelling.push_str(&suffix),
                                _ => return err("Unexpected float suffix"),
                                }
                            }
                            else if current.map(|ch| ch == '_' || ch.is_alphabetic()).unwrap_or(false) {
                                return err("Unexpected float suffix");
                            }

                            rv.push(spelled(&number_spelling)?.into());
                            continue 'outer;
                        }
                        else {
                            break;
                        }
                    }
                    rv.push(spelled(&number_spelling)?.into());
                    continue 'outer;
                }
                // Punctuation?
                else if c as u32 <= 0xFF
                {
                    let mut start = match SYMS.iter().position(|v| v[0] == (c as u8))
                        {
                        Some(start) => start,
                        None => {
                            eprint!("Unknown operator character '{}'\r\n", c);
                            return err("Unknown operator")
                            },
                        };
                    let mut end = start+1;
                    while end < SYMS.len() && SYMS[end][0] == c as u8 {
                        end += 1;
                    }

                    let mut ofs = 1;
                    loop
                    {
                        let syms = &SYMS[start..end];
                        assert_eq!(ofs, syms[0].len(), "{:?}", syms[0]);
                        c = some_else!(it.consume() => break);
                        let step = match syms[1..].iter().position(|v| v[ofs] == (c as u8))
                            {
                            Some(s) => s+1,
                            None => break,
                            };
                        start += step;
                        end = start+1;
                        while end < syms.len() && syms[end][ofs] == c as u8 {
                            end += 1;
                        }
                        ofs += 1;
                    }
                    assert_eq!(SYMS[start].len(), ofs);
                    for (i,&punct) in Iterator::enumerate(SYMS[start].iter())
                    {
                        let sep = if i != SYMS[start].len() - 1 ||
                            (!it.is_complete() && starts_punctuation(&it, c))
                        {
                            Spacing::Joint
                        }
                        else {
                            Spacing::Alone
                        };
                        rv.push(Punct::from_char(punct as char, sep, crate::Span::call_site()).into());
                    }
                }
                else
                {
                    return err("Unexpected character");
                }
            }
        }

        Ok(TokenStream::from_trees(group_delimiters(rv)?))
    }
}

#[cfg(test)]
mod tests {
    use crate::*;
    use ::std::str::FromStr;
    
    macro_rules! assert_tt_matches {
        ($have:expr, $exp:expr) => {{
            let e = $exp;
            let h = match $have
                {
                Some(h) => h,
                None => panic!("Unexpected end of stream (expecting {:?})", e),
                };
            assert!( crate::token_tree::tt_eq(&h, &e), "Expected {:?} got {:?}", e, h );
        }};
        ($have:expr) => {{
            match $have
            {
            Some(h) => panic!("Expected end of stream, found {:?}", h),
            None => {},
            }
        }};
    }

    #[test]
    fn char_literals()
    {
        TokenStream::from_str("'!'").expect("failed to parse");
        TokenStream::from_str("'\\u{2764}'").expect("failed to parse Unicode escape");
    }

    #[test]
    fn lifetime()
    {
        let rv = TokenStream::from_str("foo::bar<'a>").expect("Failed to parse");

        let mut it = rv.into_iter();
        assert_tt_matches!(it.next(), Ident::new("foo", Span::call_site()).into());
        assert_tt_matches!(it.next(), Punct::from_char(':', Spacing::Joint, crate::Span::call_site()).into());
        assert_tt_matches!(it.next(), Punct::from_char(':', Spacing::Alone, crate::Span::call_site()).into());
        assert_tt_matches!(it.next(), Ident::new("bar", Span::call_site()).into());
        assert_tt_matches!(it.next(), Punct::from_char('<', Spacing::Alone, crate::Span::call_site()).into());
        assert_tt_matches!(it.next(), Punct::from_char('\'', Spacing::Joint, crate::Span::call_site()).into());
        assert_tt_matches!(it.next(), Ident::new("a", Span::call_site()).into());
        assert_tt_matches!(it.next(), Punct::from_char('>', Spacing::Alone, crate::Span::call_site()).into());
        assert_tt_matches!(it.next());
    }

    #[test]
    fn trailing_zero()
    {
        let rv = TokenStream::from_str("0").expect("Failed to parse");

        let mut it = rv.into_iter();
        assert_tt_matches!(it.next(), Literal::from_spelling("0", Span::call_site()).unwrap().into());
        assert_tt_matches!(it.next());
    }
    #[test]
    fn tuple_index_method()
    {
        let rv = TokenStream::from_str("key . 1 . def_id").expect("Failed to parse");
        let mut it = rv.into_iter();
        assert_tt_matches!(it.next(), Ident::new("key", Span::call_site()).into());
        assert_tt_matches!(it.next(), Punct::from_char('.', Spacing::Alone, crate::Span::call_site()).into());
        assert_tt_matches!(it.next(), Literal::from_spelling("1", Span::call_site()).unwrap().into());
        assert_tt_matches!(it.next(), Punct::from_char('.', Spacing::Alone, crate::Span::call_site()).into());
        assert_tt_matches!(it.next(), Ident::new("def_id", Span::call_site()).into());
        assert_tt_matches!(it.next());
    }
}
