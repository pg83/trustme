use crate::*;

use crate::protocol::Token;
use crate::protocol::{Reader,Writer};
use crate::token_stream::{JOINED_AFTER, JOINED_OPEN};

/// Receive a token stream from the compiler
pub fn recv_token_stream<R: ::std::io::Read>(reader: R) -> TokenStream
{
    let mut s = Reader::new(reader);
    // A `SpanRef` applies to every token after it until the next one, and the
    // stream opens at the call site; a token handed back to the compiler with
    // this span keeps the resolution context it was written with.
    let mut span = Span::call_site();
    // A `Joined` marker applies to the token after it: joint punctuation, or a
    // join the macro cannot see but the compiler gets back with the stream. One
    // before an opening delimiter belongs to the group, not to its first tree.
    let mut joined = 0;
    return get_subtree(&mut s, Close::Stream, &mut span, &mut joined);

    /// What ends the stream being read: the end of the input, a closing
    /// delimiter, or the end of an invisible group.
    #[derive(Clone, Copy, PartialEq)]
    enum Close {
        Stream,
        Symbol(&'static str),
        Invisible,
    }

    fn get_subtree<R: ::std::io::Read>(s: &mut Reader<R>, end: Close, span: &mut Span, joined: &mut u8) -> TokenStream {
        let mut toks: Vec<TokenTree> = Vec::new();
        let mut hidden: Vec<u8> = Vec::new();
        while let Some(t) = s.read_ent()
        {
            let mut open_joined = false;
            let tt = match t
                {
                Token::SpanRef(idx) => {
                    *span = crate::Span::from_raw(idx);
                    continue
                    },
                Token::Joined(kind) => {
                    *joined = kind;
                    continue
                    },
                Token::SpanDef(sd) => {
                    crate::Span::define(sd.idx,
                        if sd.parent_idx == 0 { None } else { Some(crate::Span::from_raw(sd.parent_idx - 1)) },
                        crate::span::SourceFile( sd.path.into(), sd.is_path_real ),
                        sd.start_line .. sd.end_line,
                        sd.start_ofs .. sd.end_ofs,
                        );
                    continue
                    },
                Token::EndOfStream if end == Close::Stream => { hidden.resize(toks.len(), 0); return TokenStream::from_received(toks, hidden) },
                Token::Symbol(ref s) if matches!(end, Close::Symbol(e) if e == s.as_str()) => { hidden.resize(toks.len(), 0); return TokenStream::from_received(toks, hidden) },
                Token::CloseInvisible if end == Close::Invisible => { hidden.resize(toks.len(), 0); return TokenStream::from_received(toks, hidden) },
                Token::Symbol(ref s) if s == "" => panic!("Unexpected end-of-stream marker"),
                Token::EndOfStream => panic!("Unexpected end-of-stream marker"),
                Token::CloseInvisible => panic!("Unexpected end of an invisible group"),
                Token::OpenInvisible => {
                    open_joined = ::std::mem::take(joined) != 0;
                    group(Delimiter::None, s, Close::Invisible, span, joined).into()
                    },
                Token::Symbol(sym) => {
                    match &sym[..]
                    {
                    "{" | "[" | "(" => {
                        open_joined = ::std::mem::take(joined) != 0;
                        match &sym[..]
                        {
                        "{" => group(Delimiter::Brace, s, Close::Symbol("}"), span, joined).into(),
                        "[" => group(Delimiter::Bracket, s, Close::Symbol("]"), span, joined).into(),
                        _ => group(Delimiter::Parenthesis, s, Close::Symbol(")"), span, joined).into(),
                        }
                        },
                    _ => {
                        let mut it = sym.chars();
                        let mut c = it.next().unwrap();
                        while let Some(nc) = it.next()
                        {
                            toks.push(Punct::from_char(c, Spacing::Joint, *span).into());
                            c = nc;
                        }
                        Punct::from_char(c, Spacing::Alone, *span).into()
                        },
                    }
                    },
                Token::Ident(val) => match val.strip_prefix("r#") {
                    Some(name) => Ident::from_name(name, true, *span).into(),
                    None => Ident::from_name(&val, false, *span).into(),
                    },
                Token::Lifetime(val) => {
                    toks.push(Punct::from_char('\'', Spacing::Joint, *span).into());
                    match val.strip_prefix("r#") {
                        Some(name) => Ident::from_name(name, true, *span).into(),
                        None => Ident::from_name(&val, false, *span).into(),
                        }
                    },
                Token::String(val) => spanned(Literal::string(&val), *span),
                Token::ByteString(val) => spanned(Literal::byte_string(&val), *span),
                Token::Char(ch) => spanned(Literal::character(ch), *span),
                Token::Unsigned(val, ty) => spanned(integer(&val.to_string(), ty, "u"), *span),
                Token::Signed(val, ty) => spanned(integer(&val.to_string(), ty, "i"), *span),
                Token::Float(val, ty) => spanned(match ty {
                    32 => Literal::f32_suffixed(val as f32),
                    64 => Literal::f64_suffixed(val),
                    _ => Literal::f64_unsuffixed(val),
                    }, *span),
                Token::RawLiteral(val) => match Literal::from_spelling(&val, *span) {
                    Some(literal) => literal.into(),
                    None => panic!("Raw literal `{}` from the compiler is no literal", val),
                    },
                };
            toks.push(tt);
            hidden.resize(toks.len(), 0);
            let last = toks.len() - 1;
            if open_joined {
                hidden[last] |= JOINED_OPEN;
            }
            if *joined != 0 {
                match &mut toks[last] {
                TokenTree::Punct(p) if *joined == 1 => p.joint = true,
                _ => hidden[last] |= JOINED_AFTER,
                }
                *joined = 0;
            }
        }
        panic!("Unexpected EOF")
    }

    /// A delimited group opens with the span in force at its opening delimiter;
    /// its contents carry on updating the same running span.
    fn group<R: ::std::io::Read>(delimiter: Delimiter, s: &mut Reader<R>, end: Close, span: &mut Span, joined: &mut u8) -> Group {
        let open = *span;
        let stream = get_subtree(s, end, span, joined);
        Group {
            delimiter,
            stream,
            span: crate::token_tree::DelimSpan { open, close: *span, entire: open },
        }
    }

    fn spanned(mut literal: Literal, span: Span) -> TokenTree {
        literal.set_span(span);
        literal.into()
    }

    /// An integer literal of the compiler's value and width code (0 unsuffixed,
    /// 1 pointer-sized, else the bit width).
    fn integer(digits: &str, width: u8, sign: &str) -> Literal {
        let suffix = match width {
            0 => String::new(),
            1 => format!("{}size", sign),
            bits => format!("{}{}", sign, bits),
        };
        match Literal::from_spelling(&format!("{}{}", digits, suffix), Span::call_site()) {
            Some(literal) => literal,
            None => panic!("Integer `{}{}` from the compiler is no literal", digits, suffix),
        }
    }
}

// --------------------------------------------------------------------
// 
// --------------------------------------------------------------------


/// Send a token stream back to the compiler
pub fn send_token_stream<T: ::std::io::Write>(out_stream: T, ts: TokenStream)
{
    /// The compiler reads the stream at the call site until told otherwise, so a
    /// `SpanRef` only has to go out where the span changes.
    fn set_span<T: ::std::io::Write>(s: &mut Writer<T>, last: &mut usize, span: Span)
    {
        let raw = span.to_raw();
        if raw != *last {
            *last = raw;
            s.write_ent(Token::SpanRef(raw));
        }
    }

    fn inner<T: ::std::io::Write>(s: &mut Writer<T>, ts: TokenStream, last: &mut usize)
    {
        // Joins the compiler handed over go back with a stream the macro left as it was
        let hidden = ts.hidden_joins();
        let hidden_at = |index: usize| hidden.get(index).copied().unwrap_or(0) & JOINED_AFTER != 0;
        let open_hidden_at = |index: usize| hidden.get(index).copied().unwrap_or(0) & JOINED_OPEN != 0;
        let mut index = 0;
        let mut it = ts.into_trees().into_iter().peekable();
        while let Some(t) = it.next()
        {
            let this = index;
            index += 1;
            match t
            {
            TokenTree::Group(sg) => {
                set_span(s, last, sg.span.open);
                if sg.delimiter != Delimiter::None && open_hidden_at(this) {
                    s.write_ent(Token::Joined(2));
                }
                match sg.delimiter
                {
                Delimiter::None => {},
                Delimiter::Brace => s.write_sym_1('{'),
                Delimiter::Parenthesis => s.write_sym_1('('),
                Delimiter::Bracket => s.write_sym_1('['),
                }
                inner(s, sg.stream, last);
                set_span(s, last, sg.span.close);
                if hidden_at(this) {
                    s.write_ent(Token::Joined(2));
                }
                match sg.delimiter
                {
                Delimiter::None => {},
                Delimiter::Brace => s.write_sym_1('}'),
                Delimiter::Parenthesis => s.write_sym_1(')'),
                Delimiter::Bracket => s.write_sym_1(']'),
                }
                },
            TokenTree::Ident(i) => {
                set_span(s, last, i.span);
                if hidden_at(this) {
                    s.write_ent(Token::Joined(2));
                }
                if i.is_raw {
                    s.write_ent(Token::Ident(format!("r#{}", i.name())));
                }
                else {
                    s.write_ent(Token::Ident(i.name().to_owned()));
                }
                },
            TokenTree::Punct(p) => {
                set_span(s, last, p.span);
                if p.as_char() == '\'' {
                    // Get next, must be ident, push lifetime
                    let v = match it.next()
                        {
                        Some(TokenTree::Ident(ident)) if ident.is_raw => format!("r#{}", ident.name()),
                        Some(TokenTree::Ident(ident)) => ident.name().to_owned(),
                        _ => panic!("Punct('\\'') not followed by an ident"),
                        };
                    index += 1;
                    if hidden_at(this + 1) {
                        s.write_ent(Token::Joined(2));
                    }
                    s.write_ent(Token::Lifetime(v));
                }
                else if p.spacing() == Spacing::Alone {
                    if hidden_at(this) {
                        s.write_ent(Token::Joined(2));
                    }
                    s.write_sym_1(p.as_char());
                }
                else {
                    // Joint punct up to the first Alone one, or the first token that is no punct
                    let mut chars = String::new();
                    chars.push(p.as_char());
                    let mut end = this;
                    let mut end_joint = true;
                    loop
                    {
                        match it.peek()
                        {
                        Some(TokenTree::Punct(next)) if next.as_char() != '\'' => {
                            chars.push(next.as_char());
                            let joint = next.spacing() == Spacing::Joint;
                            it.next();
                            end = index;
                            index += 1;
                            end_joint = joint;
                            if !joint {
                                break;
                            }
                            },
                        _ => break,
                        }
                    }
                    if hidden_at(end) {
                        s.write_ent(Token::Joined(2));
                    }
                    else if end_joint {
                        s.write_ent(Token::Joined(1));
                    }
                    s.write_sym(chars.as_bytes());
                }
                },
            TokenTree::Literal(literal) => {
                set_span(s, last, literal.span);
                if hidden_at(this) {
                    s.write_ent(Token::Joined(2));
                }
                s.write_ent(Token::RawLiteral(literal.to_string()));
                },
            }
        }
    }

    let mut s = Writer::new(out_stream);
    // Send the token stream - the compiler starts it off at the call site
    let mut last = 1;
    inner(&mut s, ts, &mut last);
    // Empty symbol indicates EOF
    s.write_sym(b"");
}

pub fn send_panic<T: ::std::io::Write>(out_stream: T, message: Option<&str>)
{
    Writer::new(out_stream).write_panic(message);
}


#[cfg(test)]
mod write_tests {
    use crate::*;

    #[test]
    fn empty()
    {
        let mut out = Vec::new();
        super::send_token_stream(&mut out, TokenStream::default());

        assert_eq!(out, &[
            0,0,
            ]);
    }

    #[test]
    fn symbols()
    {
        let mut out = Vec::new();
        super::send_token_stream(&mut out, TokenStream::from_trees(vec![
                Punct::new('<', Spacing::Joint).into(),
                Punct::new('<', Spacing::Alone).into(),

                Punct::new('<', Spacing::Alone).into(),
            ]));

        assert_eq!(out, &[
            0,2,b'<',b'<',
            0,1,b'<',
            0,0,
            ]);
    }

    #[test]
    fn lifetime()
    {
        let mut out = Vec::new();
        super::send_token_stream(&mut out, TokenStream::from_trees(vec![
                Punct::new('\'', Spacing::Joint).into(),
                Ident::new("a", Span::call_site()).into(),
            ]));

        assert_eq!(out, &[
            2, 1, b'a', // Lifetime
            0,0,    // Terminator
            ]);
    }
}
#[cfg(test)]
mod read_tests {
    use crate::*;

    macro_rules! assert_tt_matches {
        ($have:expr, $exp:expr) => {{
            let e = $exp;
            let h = match $have
                {
                Some(h) => h,
                None => panic!("Unexpected end of stream (expecting {:?})", e),
                };
            assert!( crate::token_tree::tt_eq(&h, &e) );
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
    fn lifetime()
    {
        let rv = super::recv_token_stream(&[
            2, 1, b'a', // Lifetime
            0,0,    // Terminator
            ][..]);
        let mut it = rv.into_iter();
        assert_tt_matches!(it.next(), Punct::new('\'', Spacing::Joint).into());
        assert_tt_matches!(it.next(), Ident::new("a", Span::call_site()).into());
        assert_tt_matches!(it.next());
    }
}
