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
        crate::TokenTree::Punct(ref punct) if punct.ch == '(' || punct.ch == '[' || punct.ch == '{' => {
            groups.push((punct.ch, current));
            current = Vec::new();
            },
        crate::TokenTree::Punct(ref punct) if punct.ch == ')' || punct.ch == ']' || punct.ch == '}' => {
            let (open, mut parent) = match groups.pop() {
                Some(group) => group,
                None => return Err(LexError { inner: "Unexpected closing delimiter" }),
                };
            let delimiter = match (open, punct.ch) {
                ('(', ')') => crate::Delimiter::Parenthesis,
                ('[', ']') => crate::Delimiter::Bracket,
                ('{', '}') => crate::Delimiter::Brace,
                _ => return Err(LexError { inner: "Mismatched closing delimiter" }),
                };
            parent.push(crate::Group::new(delimiter, TokenStream { inner: current }).into());
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

        fn get_unicode_escape<T: Iterator<Item=char>>(it: &mut CharStream<T>) -> Result<char, &'static str>
        {
            if it.consume() != Some('{') {
                return Err("Expected `{` after `\\u` in literal");
            }

            let mut value = 0u32;
            let mut digits = 0;
            loop {
                let c = match it.consume() {
                    Some(c) => c,
                    None => return Err("Unterminated `\\u` escape"),
                };
                if c == '}' {
                    break;
                }
                if c == '_' {
                    continue;
                }
                let digit = match c.to_digit(16) {
                    Some(digit) => digit,
                    None => return Err("Invalid hex digit in `\\u` escape"),
                };
                digits += 1;
                if digits > 6 {
                    return Err("Overlong `\\u` escape");
                }
                value = value * 16 + digit;
            }
            if digits == 0 {
                return Err("Empty `\\u` escape");
            }
            ::std::char::from_u32(value).ok_or("Invalid Unicode scalar in `\\u` escape")
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
                    rv.push(Punct::new('\'', Spacing::Joint).into());
                    rv.push(Ident { is_raw: false, val: ident, span: crate::Span::call_site() }.into());
                }
                else {
                    // Char lit
                    let new_c = if c == '\\' {
                            match match it.consume()
                                {
                                Some(c) => c,
                                None => return err("Unterminated char literal"),
                                }
                            {
                            '0' => '\0',
                            'n' => '\n',
                            'r' => '\r',
                            't' => '\t',
                            '\\' => '\\',
                            '\'' => '\'',
                            '"' => '"',
                            'u' => match get_unicode_escape(&mut it) {
                                Ok(c) => c,
                                Err(e) => return err(e),
                                },
                            c @ _ => panic!("TODO: char literal with escape - '\\{}'", c),
                            }
                        }
                        else {
                            c
                        };
                    rv.push(Literal::character(new_c).into());
                    match it.consume()
                    {
                    Some('\'') => {},
                    Some(c) => {
                        debug!("Stray charcter '{}'", c);
                        return err("Multiple characters in char literal");
                        },
                    None => {
                        return err("Unterminated char literal");
                        },
                    }
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
                                rv.push(Ident { is_raw: false, val: s, span: crate::Span::call_site() }.into());
                            }
                            else {
                                rv.push(Ident::new(ident_str.into(), crate::Span::call_site()).into());
                            }
                            while hashes > 0 {
                                rv.push(Punct::new('#', if hashes == 1 { Spacing::Alone } else { Spacing::Joint }).into());
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
                        rv.push(Literal {
                            span: crate::Span::call_site(),
                            val: crate::token_tree::LiteralValue::Raw(spelling)
                        }.into());
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
                        rv.push(Literal {
                            span: crate::Span::call_site(),
                            val: crate::token_tree::LiteralValue::Raw(spelling),
                        }.into());
                        continue 'outer;
                    }
                    else if c == '\"'
                    {
                        // String literal
                        let mut s = String::new();
                        loop
                        {
                            c = some_else!(it.consume() => return err("str eof"));
                            if c == '"' {
                                it.consume();
                                break ;
                            }
                            else if c == '\\' {
                                match some_else!(it.consume() => return err("str eof"))
                                {
                                'n' => s.push('\n'),
                                'r' => s.push('\r'),
                                't' => s.push('\t'),
                                '0' => s.push('\0'),
                                '\\' => s.push('\\'),
                                '\'' => s.push('\''),
                                '"' => s.push('"'),
                                'x' => {
                                    let mut v = 0u32;
                                    for _ in 0 .. 2 {
                                        let d = some_else!(it.consume() => return err("str eof"));
                                        v = v * 16 + some_else!(d.to_digit(16) => return err("Invalid hex digit in `\\x` escape"));
                                    }
                                    s.push(some_else!(::std::char::from_u32(v) => return err("Invalid `\\x` escape")));
                                    },
                                'u' => {
                                    s.push(match get_unicode_escape(&mut it) {
                                        Ok(c) => c,
                                        Err(e) => return err(e),
                                        });
                                    },
                                // A backslash at end-of-line eats the newline and the next line's leading whitespace
                                '\n' | '\r' => {
                                    while it.next().map(|c| c.is_whitespace()).unwrap_or(false) {
                                        it.consume();
                                    }
                                    },
                                c @ _ => panic!("Unknown escape in string {}", c),
                                }
                            }
                            else {
                                s.push(c);
                            }
                        }
                        rv.push(Literal {
                            span: crate::Span::call_site(),
                            val: if is_byte {
                                crate::token_tree::LiteralValue::ByteString(s.into_bytes())
                            } else {
                                crate::token_tree::LiteralValue::String(s)
                            }
                        }.into());
                        continue 'outer;
                    }
                    else
                    {
                        // Could be an ident starting with 'b', or it's just 'b'
                        // - Fall through
                        let ident = get_ident(&mut it, "b".into());
                        rv.push(Ident { span: Span::call_site(), is_raw: false, val: ident }.into());
                        continue 'outer;
                    }
                }

                // Identifier.
                if c.is_alphabetic() || c == '_'
                {
                    let ident = get_ident(&mut it, String::new());
                    if false && ident == "_" {
                        rv.push(Punct::new('_', Spacing::Alone).into());
                    }
                    else {
                        rv.push(Ident { span: Span::call_site(), is_raw: false, val: ident }.into());
                    }
                }
                else if c.is_digit(10)
                {
                    let base =
                        if c == '0' {
                            match it.consume()
                            {
                            Some('x') => { it.consume(); 16 },
                            Some('o') => { it.consume(); 8 },
                            Some('b') => { it.consume(); 2 },
                            None => {
                                rv.push(Literal::new_u(0, 0).into());
                                continue 'outer;
                                },
                            _ => 10,
                            }
                        }
                        else {
                            10
                        };
                    let mut v = 0;
                    let mut c = it.cur();
                    'int: loop
                    {
                        while c == '_' {
                            c = some_else!( it.consume() => { break 'int; } );
                        }
                        if c == 'u' || c == 'i' {
                            let s = get_ident(&mut it, String::new());
                            rv.push(match &*s
                                {
                                "u8"    => Literal::new_u(v,   8), "i8"    => Literal::new_s(v as i128,   8),
                                "u16"   => Literal::new_u(v,  16), "i16"   => Literal::new_s(v as i128,  16),
                                "u32"   => Literal::new_u(v,  32), "i32"   => Literal::new_s(v as i128,  32),
                                "u64"   => Literal::new_u(v,  64), "i64"   => Literal::new_s(v as i128,  64),
                                "u128"  => Literal::new_u(v, 128), "i128"  => Literal::new_s(v as i128, 128),
                                "usize" => Literal::new_u(v,   1), "isize" => Literal::new_s(v as i128,   1),
                                _ => return err("Unexpected integer suffix"),
                                }.into());
                            continue 'outer;
                        }
                        else if let Some(d) = c.to_digit(base) {
                            v *= base as u128;
                            v += d as u128;
                            c = some_else!( it.consume() => { break 'int; } );
                        }
                        else if c == '.' {
                            panic!("TODO: Floating point");
                        }
                        else {
                            break;
                        }
                    }
                    rv.push(Literal::new_u(v, 0).into());
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
                    for (i,&c) in Iterator::enumerate(SYMS[start].iter())
                    {
                        let sep = if i == SYMS[start].len() - 1 { Spacing::Alone } else { Spacing::Joint };
                        rv.push(Punct::new(c as char, sep).into());
                    }
                }
                else
                {
                    return err("Unexpected character");
                }
            }
        }

        Ok(TokenStream {
            inner: group_delimiters(rv)?,
            })
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

        let mut it = rv.inner.into_iter();
        assert_tt_matches!(it.next(), Ident::new("foo", Span::call_site()).into());
        assert_tt_matches!(it.next(), Punct::new(':', Spacing::Joint).into());
        assert_tt_matches!(it.next(), Punct::new(':', Spacing::Alone).into());
        assert_tt_matches!(it.next(), Ident::new("bar", Span::call_site()).into());
        assert_tt_matches!(it.next(), Punct::new('<', Spacing::Alone).into());
        assert_tt_matches!(it.next(), Punct::new('\'', Spacing::Joint).into());
        assert_tt_matches!(it.next(), Ident::new("a", Span::call_site()).into());
        assert_tt_matches!(it.next(), Punct::new('>', Spacing::Alone).into());
        assert_tt_matches!(it.next());
    }

    #[test]
    fn trailing_zero()
    {
        let rv = TokenStream::from_str("0").expect("Failed to parse");

        let mut it = rv.inner.into_iter();
        assert_tt_matches!(it.next(), Literal::new_u(0,0).into());
        assert_tt_matches!(it.next());
    }
    #[test]
    fn tuple_index_method()
    {
        let rv = TokenStream::from_str("key . 1 . def_id").expect("Failed to parse");
        let mut it = rv.inner.into_iter();
        assert_tt_matches!(it.next(), Ident::new("key", Span::call_site()).into());
        assert_tt_matches!(it.next(), Punct::new('.', Spacing::Alone).into());
        assert_tt_matches!(it.next(), Literal::new_u(1,0).into());
        assert_tt_matches!(it.next(), Punct::new('.', Spacing::Alone).into());
        assert_tt_matches!(it.next(), Ident::new("def_id", Span::call_site()).into());
        assert_tt_matches!(it.next());
    }
}
