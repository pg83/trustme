use crate::Span;
use crate::TokenStream;
use crate::symbol::Symbol;
use crate::escape::{EscapeOptions, escape_bytes};

// 1.29
#[derive(Clone)]
pub enum TokenTree
{
    Group(Group),
    Ident(Ident),
    Punct(Punct),
    Literal(Literal),
}
impl ::std::fmt::Display for TokenTree
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result
    {
        match self
        {
        &TokenTree::Group(ref v) => v.fmt(f),
        &TokenTree::Ident(ref v) => v.fmt(f),
        &TokenTree::Punct(ref v) => v.fmt(f),
        &TokenTree::Literal(ref v) => v.fmt(f),
        }
    }
}
impl ::std::fmt::Debug for TokenTree
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result
    {
        match self
        {
        &TokenTree::Group(ref v) => v.fmt(f),
        &TokenTree::Ident(ref v) => v.fmt(f),
        &TokenTree::Punct(ref v) => v.fmt(f),
        &TokenTree::Literal(ref v) => v.fmt(f),
        }
    }
}
impl TokenTree
{
    pub fn span(&self) -> Span {
        match self
        {
        &TokenTree::Group(ref v) => v.span(),
        &TokenTree::Ident(ref v) => v.span,
        &TokenTree::Punct(ref v) => v.span,
        &TokenTree::Literal(ref v) => v.span,
        }
    }
    pub fn set_span(&mut self, span: Span) {
        match self
        {
        &mut TokenTree::Group(ref mut v) => v.set_span(span),
        &mut TokenTree::Ident(ref mut v) => v.set_span(span),
        &mut TokenTree::Punct(ref mut v) => v.set_span(span),
        &mut TokenTree::Literal(ref mut v) => v.set_span(span),
        }
    }
}
impl From<Group> for TokenTree {
    fn from(g: Group) -> Self {
        TokenTree::Group(g)
    }
}
impl From<Ident> for TokenTree {
    fn from(v: Ident) -> Self {
        TokenTree::Ident(v)
    }
}
impl From<Punct> for TokenTree {
    fn from(v: Punct) -> Self {
        TokenTree::Punct(v)
    }
}
impl From<Literal> for TokenTree {
    fn from(v: Literal) -> Self {
        TokenTree::Literal(v)
    }
}

/// The spans of a delimited group: its opening and closing delimiters, and the
/// whole group, as upstream's bridge keeps them.
#[derive(Copy,Clone)]
pub(crate) struct DelimSpan {
    pub(crate) open: Span,
    pub(crate) close: Span,
    pub(crate) entire: Span,
}
impl DelimSpan {
    pub(crate) fn from_single(span: Span) -> DelimSpan {
        DelimSpan { open: span, close: span, entire: span }
    }
}

#[derive(Clone)]
pub struct Group {
    pub(crate) delimiter: Delimiter,
    pub(crate) stream: TokenStream,
    pub(crate) span: DelimSpan,
}
impl ::std::fmt::Display for Group
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result
    {
        match self.delimiter
        {
        Delimiter::Parenthesis => f.write_str("(")?,
        Delimiter::Brace => f.write_str("{")?,
        Delimiter::Bracket => f.write_str("[")?,
        Delimiter::None => {},
        }
        self.stream.fmt(f)?;
        match self.delimiter
        {
        Delimiter::Parenthesis => f.write_str(")")?,
        Delimiter::Brace => f.write_str("}")?,
        Delimiter::Bracket => f.write_str("]")?,
        Delimiter::None => f.write_str(" ")?,
        }
        Ok(())
    }
}
impl ::std::fmt::Debug for Group
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result
    {
        f.debug_struct("Group")
            .field("delimiter", &self.delimiter)
            .field("stream", &self.stream)
            .finish()
    }
}

impl Group {
    pub fn new(delimiter: Delimiter, stream: TokenStream) -> Group {
        Group {
            delimiter,
            stream,
            span: DelimSpan::from_single(Span::call_site()),
        }
    }
    pub fn delimiter(&self) -> Delimiter {
        self.delimiter
    }
    pub fn stream(&self) -> TokenStream {
        self.stream.clone()
    }

    pub fn span(&self) -> Span {
        self.span.entire
    }
    pub fn set_span(&mut self, span: Span) {
        self.span = DelimSpan::from_single(span);
    }
    pub fn span_open(&self) -> Span {
        self.span.open
    }
    pub fn span_close(&self) -> Span {
        self.span.close
    }
}

#[derive(Copy,Clone,Debug,Eq,PartialEq)]
pub enum Delimiter {
    Parenthesis,
    Brace,
    Bracket,
    None,
}

#[derive(Clone)]
pub struct Ident {
    pub(crate) sym: Symbol,
    pub(crate) is_raw: bool,
    pub(crate) span: Span,
}

fn validate_ident(string: &str) {
    if string.is_empty() {
        panic!("Ident is not allowed to be empty; use Option<Ident>");
    }
    if string.bytes().all(|digit| digit >= b'0' && digit <= b'9') {
        panic!("Ident cannot be a number; use Literal instead");
    }

    let mut chars = string.chars();
    let first = chars.next().unwrap();
    if first != '_' && !first.is_alphabetic()
        || !chars.all(|ch| ch == '_' || ch.is_alphanumeric())
    {
        panic!("{:?} is not a valid Ident", string);
    }
}

impl Ident {
    pub fn new(string: &str, span: Span) -> Ident {
        validate_ident(string);
        Ident { sym: Symbol::intern(string), is_raw: false, span }
    }
    // 1.47
    pub fn new_raw(string: &str, span: Span) -> Ident {
        validate_ident(string);
        Ident { sym: Symbol::intern(string), is_raw: true, span }
    }
    /// An identifier the lexer or the compiler produced, without the validation
    /// a macro's own `Ident::new` gets.
    pub(crate) fn from_name(name: &str, is_raw: bool, span: Span) -> Ident {
        Ident { sym: Symbol::intern(name), is_raw, span }
    }
    pub(crate) fn name(&self) -> &'static str {
        self.sym.as_str()
    }
    pub fn span(&self) -> Span {
        self.span
    }
    pub fn set_span(&mut self, span: Span) {
        self.span = span;
    }
}
impl ::std::fmt::Display for Ident
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result
    {
        if self.is_raw {
            f.write_str("r#")?;
        }
        f.write_str(self.name())?;
        Ok( () )
    }
}
impl ::std::fmt::Debug for Ident
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result
    {
        f.debug_struct("Ident")
            .field("ident", &self.to_string())
            .finish()
    }
}

#[derive(Clone)]
pub struct Punct {
    pub(crate) ch: u8,
    pub(crate) joint: bool,
    pub(crate) span: Span,
}
impl ::std::fmt::Display for Punct
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result
    {
        write!(f, "{}", self.as_char())
    }
}
impl ::std::fmt::Debug for Punct
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result
    {
        f.debug_struct("Punct")
            .field("ch", &self.as_char())
            .field("spacing", &self.spacing())
            .finish()
    }
}
impl Punct {
    pub fn new(ch: char, spacing: Spacing) -> Punct {
        const LEGAL_CHARS: &[char] = &[
            '=', '<', '>', '!', '~', '+', '-', '*', '/', '%', '^', '&', '|', '@', '.', ',', ';',
            ':', '#', '$', '?', '\'',
        ];
        if !LEGAL_CHARS.contains(&ch) {
            panic!("unsupported character `{:?}`", ch);
        }
        Punct::from_char(ch, spacing, Span::call_site())
    }
    /// A punctuation character the lexer produced - a delimiter among them,
    /// before it is grouped.
    pub(crate) fn from_char(ch: char, spacing: Spacing, span: Span) -> Punct {
        Punct {
            ch: ch as u8,
            joint: spacing == Spacing::Joint,
            span,
        }
    }
    pub fn as_char(&self) -> char {
        self.ch as char
    }
    pub fn spacing(&self) -> Spacing {
        if self.joint { Spacing::Joint } else { Spacing::Alone }
    }
    pub fn span(&self) -> Span {
        self.span
    }
    pub fn set_span(&mut self, span: Span) {
        self.span = span;
    }
}
impl ::std::cmp::PartialEq<char> for Punct {
    fn eq(&self, rhs: &char) -> bool {
        self.as_char() == *rhs
    }
}
impl ::std::cmp::PartialEq<Punct> for char {
    fn eq(&self, rhs: &Punct) -> bool {
        *self == rhs.as_char()
    }
}
#[derive(Copy,Clone,Debug,PartialEq,Eq)]
pub enum Spacing {
    Alone,
    Joint,
}

/// The kind of a literal token, as upstream's bridge has it: the token's text
/// is its symbol and suffix, the kind says what surrounds them.
#[derive(Copy,Clone,Debug,PartialEq,Eq)]
pub(crate) enum LitKind {
    Byte,
    Char,
    Integer,
    Float,
    Str,
    StrRaw(u8),
    ByteStr,
    ByteStrRaw(u8),
    CStr,
    CStrRaw(u8),
    ErrWithGuar,
}

#[derive(Clone)]
pub struct Literal {
    pub(crate) kind: LitKind,
    pub(crate) symbol: Symbol,
    pub(crate) suffix: Option<Symbol>,
    pub(crate) span: Span,
}

macro_rules! suffixed_int_literals {
    ($($name:ident => $kind:ident,)*) => ($(
        pub fn $name(n: $kind) -> Literal {
            Literal::new(LitKind::Integer, &n.to_string(), Some(stringify!($kind)))
        }
    )*)
}

macro_rules! unsuffixed_int_literals {
    ($($name:ident => $kind:ident,)*) => ($(
        pub fn $name(n: $kind) -> Literal {
            Literal::new(LitKind::Integer, &n.to_string(), None)
        }
    )*)
}

impl Literal {
    fn new(kind: LitKind, value: &str, suffix: Option<&str>) -> Self {
        Literal {
            kind,
            symbol: Symbol::intern(value),
            suffix: suffix.map(Symbol::intern),
            span: Span::call_site(),
        }
    }

    suffixed_int_literals! {
        u8_suffixed => u8,
        u16_suffixed => u16,
        u32_suffixed => u32,
        u64_suffixed => u64,
        u128_suffixed => u128,
        usize_suffixed => usize,
        i8_suffixed => i8,
        i16_suffixed => i16,
        i32_suffixed => i32,
        i64_suffixed => i64,
        i128_suffixed => i128,
        isize_suffixed => isize,
    }

    unsuffixed_int_literals! {
        u8_unsuffixed => u8,
        u16_unsuffixed => u16,
        u32_unsuffixed => u32,
        u64_unsuffixed => u64,
        u128_unsuffixed => u128,
        usize_unsuffixed => usize,
        i8_unsuffixed => i8,
        i16_unsuffixed => i16,
        i32_unsuffixed => i32,
        i64_unsuffixed => i64,
        i128_unsuffixed => i128,
        isize_unsuffixed => isize,
    }

    pub fn f32_unsuffixed(n: f32) -> Literal {
        if !n.is_finite() {
            panic!("Invalid float literal {n}");
        }
        let mut repr = n.to_string();
        if !repr.contains('.') {
            repr.push_str(".0");
        }
        Literal::new(LitKind::Float, &repr, None)
    }
    pub fn f32_suffixed(n: f32) -> Literal {
        if !n.is_finite() {
            panic!("Invalid float literal {n}");
        }
        Literal::new(LitKind::Float, &n.to_string(), Some("f32"))
    }
    pub fn f64_unsuffixed(n: f64) -> Literal {
        if !n.is_finite() {
            panic!("Invalid float literal {n}");
        }
        let mut repr = n.to_string();
        if !repr.contains('.') {
            repr.push_str(".0");
        }
        Literal::new(LitKind::Float, &repr, None)
    }
    pub fn f64_suffixed(n: f64) -> Literal {
        if !n.is_finite() {
            panic!("Invalid float literal {n}");
        }
        Literal::new(LitKind::Float, &n.to_string(), Some("f64"))
    }

    pub fn string(string: &str) -> Literal {
        let escape = EscapeOptions {
            escape_single_quote: false,
            escape_double_quote: true,
            escape_nonascii: false,
        };
        let repr = escape_bytes(string.as_bytes(), escape);
        Literal::new(LitKind::Str, &repr, None)
    }
    pub fn character(ch: char) -> Literal {
        let escape = EscapeOptions {
            escape_single_quote: true,
            escape_double_quote: false,
            escape_nonascii: false,
        };
        let repr = escape_bytes(ch.encode_utf8(&mut [0u8; 4]).as_bytes(), escape);
        Literal::new(LitKind::Char, &repr, None)
    }
    // 1.79
    pub fn byte_character(byte: u8) -> Literal {
        let escape = EscapeOptions {
            escape_single_quote: true,
            escape_double_quote: false,
            escape_nonascii: true,
        };
        let repr = escape_bytes(&[byte], escape);
        Literal::new(LitKind::Byte, &repr, None)
    }
    pub fn byte_string(bytes: &[u8]) -> Literal {
        let escape = EscapeOptions {
            escape_single_quote: false,
            escape_double_quote: true,
            escape_nonascii: true,
        };
        let repr = escape_bytes(bytes, escape);
        Literal::new(LitKind::ByteStr, &repr, None)
    }
    // 1.79
    pub fn c_string(string: &::std::ffi::CStr) -> Literal {
        let escape = EscapeOptions {
            escape_single_quote: false,
            escape_double_quote: true,
            escape_nonascii: false,
        };
        let repr = escape_bytes(string.to_bytes(), escape);
        Literal::new(LitKind::CStr, &repr, None)
    }

    pub fn span(&self) -> Span {
        self.span
    }
    pub fn set_span(&mut self, span: Span) {
        self.span = span;
    }

    /// The literal a token's spelling writes: the kind from the quotes and
    /// prefix around it, the symbol from what they enclose, the suffix from
    /// what follows. `None` when the text is no single literal.
    pub(crate) fn from_spelling(text: &str, span: Span) -> Option<Literal> {
        fn quoted(text: &str, prefix: usize, quote: u8) -> Option<(usize, usize)> {
            let bytes = text.as_bytes();
            if bytes.len() < prefix + 2 || bytes[prefix] != quote {
                return None;
            }
            let close = text.rfind(quote as char)?;
            if close <= prefix {
                return None;
            }
            Some((prefix + 1, close))
        }
        fn raw(text: &str, prefix: usize) -> Option<(u8, usize, usize)> {
            let rest = &text[prefix..];
            let hashes = rest.bytes().take_while(|&b| b == b'#').count();
            let open = prefix + hashes;
            if text.as_bytes().get(open) != Some(&b'"') {
                return None;
            }
            let terminator = format!("\"{}", "#".repeat(hashes));
            let close = text.rfind(&terminator)?;
            if close <= open {
                return None;
            }
            Some((hashes as u8, open + 1, close))
        }
        let make = |kind: LitKind, body: &str, suffix: &str| Literal {
            kind,
            symbol: Symbol::intern(body),
            suffix: if suffix.is_empty() { None } else { Some(Symbol::intern(suffix)) },
            span,
        };
        let bytes = text.as_bytes();
        let first = *bytes.first()?;
        let (kind, start, end, after) = if first == b'\'' {
            let (s, e) = quoted(text, 0, b'\'')?;
            (LitKind::Char, s, e, e + 1)
        }
        else if first == b'"' {
            let (s, e) = quoted(text, 0, b'"')?;
            (LitKind::Str, s, e, e + 1)
        }
        else if text.starts_with("b'") {
            let (s, e) = quoted(text, 1, b'\'')?;
            (LitKind::Byte, s, e, e + 1)
        }
        else if text.starts_with("b\"") {
            let (s, e) = quoted(text, 1, b'"')?;
            (LitKind::ByteStr, s, e, e + 1)
        }
        else if text.starts_with("c\"") {
            let (s, e) = quoted(text, 1, b'"')?;
            (LitKind::CStr, s, e, e + 1)
        }
        else if text.starts_with("br") {
            let (n, s, e) = raw(text, 2)?;
            (LitKind::ByteStrRaw(n), s, e, e + 1 + n as usize)
        }
        else if text.starts_with("cr") {
            let (n, s, e) = raw(text, 2)?;
            (LitKind::CStrRaw(n), s, e, e + 1 + n as usize)
        }
        else if text.starts_with('r') {
            let (n, s, e) = raw(text, 1)?;
            (LitKind::StrRaw(n), s, e, e + 1 + n as usize)
        }
        else if first.is_ascii_digit() {
            let radix = if bytes.len() > 1 && first == b'0' { bytes[1] } else { 0 };
            let is_radix = radix == b'x' || radix == b'o' || radix == b'b';
            let digit = |b: u8| if radix == b'x' { b.is_ascii_hexdigit() } else { b.is_ascii_digit() };
            let mut i = if is_radix { 2 } else { 0 };
            while i < bytes.len() && (bytes[i] == b'_' || digit(bytes[i])) {
                i += 1;
            }
            let mut float = false;
            if !is_radix {
                if i < bytes.len() && bytes[i] == b'.' && (i + 1 == bytes.len() || bytes[i + 1].is_ascii_digit()) {
                    float = true;
                    i += 1;
                    while i < bytes.len() && (bytes[i] == b'_' || bytes[i].is_ascii_digit()) {
                        i += 1;
                    }
                }
                if i < bytes.len() && (bytes[i] == b'e' || bytes[i] == b'E') {
                    let mut j = i + 1;
                    if j < bytes.len() && (bytes[j] == b'+' || bytes[j] == b'-') {
                        j += 1;
                    }
                    let digits = j;
                    while j < bytes.len() && (bytes[j] == b'_' || bytes[j].is_ascii_digit()) {
                        j += 1;
                    }
                    if bytes[digits..j].iter().any(|b| b.is_ascii_digit()) {
                        float = true;
                        i = j;
                    }
                }
            }
            let suffix = &text[i..];
            if !suffix.bytes().all(|b| b == b'_' || b.is_ascii_alphanumeric()) {
                return None;
            }
            return Some(make(if float { LitKind::Float } else { LitKind::Integer }, &text[..i], suffix));
        }
        else {
            return None;
        };
        let suffix = &text[after..];
        if !suffix.bytes().all(|b| b == b'_' || b.is_ascii_alphanumeric()) {
            return None;
        }
        Some(make(kind, &text[start..end], suffix))
    }

    fn with_stringify_parts<R>(&self, f: impl FnOnce(&[&str]) -> R) -> R {
        fn get_hashes_str(num: u8) -> &'static str {
            const HASHES: &str = "\
            ################################################################\
            ################################################################\
            ################################################################\
            ################################################################\
            ";
            &HASHES[..num as usize]
        }
        let symbol = self.symbol.as_str();
        let suffix = match self.suffix {
            Some(suffix) => suffix.as_str(),
            None => "",
        };
        match self.kind {
            LitKind::Byte => f(&["b'", symbol, "'", suffix]),
            LitKind::Char => f(&["'", symbol, "'", suffix]),
            LitKind::Str => f(&["\"", symbol, "\"", suffix]),
            LitKind::StrRaw(n) => {
                let hashes = get_hashes_str(n);
                f(&["r", hashes, "\"", symbol, "\"", hashes, suffix])
            }
            LitKind::ByteStr => f(&["b\"", symbol, "\"", suffix]),
            LitKind::ByteStrRaw(n) => {
                let hashes = get_hashes_str(n);
                f(&["br", hashes, "\"", symbol, "\"", hashes, suffix])
            }
            LitKind::CStr => f(&["c\"", symbol, "\"", suffix]),
            LitKind::CStrRaw(n) => {
                let hashes = get_hashes_str(n);
                f(&["cr", hashes, "\"", symbol, "\"", hashes, suffix])
            }
            LitKind::Integer | LitKind::Float | LitKind::ErrWithGuar => f(&[symbol, suffix]),
        }
    }
}
impl ::std::str::FromStr for Literal
{
    type Err = crate::lex::LexError;
    fn from_str(v: &str) -> Result<Self,Self::Err> {
        if let Some(rest) = v.strip_prefix('-') {
            if !rest.starts_with(|c: char| c.is_ascii_digit()) {
                return Err(crate::lex::LexError { inner: "Wasn't a literal" });
            }
            let literal = Literal::from_str(rest)?;
            return match literal.kind {
                LitKind::Integer | LitKind::Float => Ok(Literal {
                    symbol: Symbol::intern(&format!("-{}", literal.symbol.as_str())),
                    ..literal
                    }),
                _ => Err(crate::lex::LexError { inner: "Wasn't a literal" }),
            };
        }
        let mut ts = crate::TokenStream::from_str(v)?.into_trees();
        match (ts.pop(), ts.is_empty()) {
        (Some(TokenTree::Literal(rv)), true) => Ok(rv),
        _ => Err(crate::lex::LexError { inner: "Wasn't a literal" }),
        }
    }
}
impl ::std::fmt::Display for Literal
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result
    {
        self.with_stringify_parts(|parts| {
            for part in parts {
                f.write_str(part)?;
            }
            Ok(())
        })
    }
}
impl ::std::fmt::Debug for Literal
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result
    {
        f.debug_struct("Literal")
            .field("kind", &self.kind)
            .field("symbol", &self.symbol)
            .field("suffix", &self.suffix)
            .finish()
    }
}

// Use for unit tests
#[cfg(test)]
pub(crate) fn tt_eq(a: &TokenTree, b: &TokenTree) -> bool
{
    match (a,b)
    {
    (&TokenTree::Group(ref a), &TokenTree::Group(ref b)) => {
        if a.delimiter != b.delimiter {
            return false;
        }
        if a.stream.trees().len() != b.stream.trees().len() {
            return false;
        }
        for (a,b) in Iterator::zip( a.stream.trees().iter(), b.stream.trees().iter() ) {
            if !tt_eq(a,b) {
                return false;
            }
        }
        true
        },
    (&TokenTree::Ident(ref a), &TokenTree::Ident(ref b)) => a.is_raw == b.is_raw && a.sym == b.sym,
    (&TokenTree::Punct(ref a), &TokenTree::Punct(ref b)) => a.ch == b.ch && a.joint == b.joint,
    (&TokenTree::Literal(ref a), &TokenTree::Literal(ref b)) => a.kind == b.kind && a.symbol == b.symbol && a.suffix == b.suffix,
    _ => false,
    }
}
