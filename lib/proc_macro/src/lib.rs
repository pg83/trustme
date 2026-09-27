// trustme's version of libproc_macro
//
// Unlike the original rustc version, this one is designed to live complely detached from its compiler.
#![allow(ellipsis_inclusive_range_patterns)]
#![feature(crate_in_paths)]
#![feature(optin_builtin_traits)]
#![feature(vec_resize_with)]
#![feature(const_vec_new)]

macro_rules! some_else {
    ($e:expr => $alt:expr) => {match $e { Some(v) => v, None => $alt }};
}

macro_rules! debug {
    ( $($t:tt)* ) => {
        if ::std::env::var_os("TRUSTME_PM_DEBUG").is_some() {
            eprintln!($($t)*)
        }
    }
}
macro_rules! note {
    ( $($t:tt)* ) => {
        if ::std::env::var_os("TRUSTME_PM_DEBUG").is_some() {
            eprintln!($($t)*)
        }
    }
}

mod span;
mod symbol;
mod escape;
mod token_tree;
/// Parse a TokenStream from a string
mod lex;
/// Raw IPC protocol
mod protocol;
/// Converts to/from TokenStream and IPC
mod serialisation;
mod diagnostic;

pub mod tracked_env;
pub mod tracked_path;

pub mod token_stream {
    use ::std::num::NonZeroU32;

    /// A handle to a stream in this process's stream store, as upstream's client
    /// holds a handle to the server's: `None` is the empty stream, a clone shares
    /// the stream, and a write to a shared one copies it first.
    pub struct TokenStream(Option<NonZeroU32>);
    impl !Send for TokenStream {}
    impl !Sync for TokenStream {}

    struct Slot {
        trees: Vec<crate::TokenTree>,
        handles: u32,
    }

    static mut SLOTS: Vec<Option<Slot>> = Vec::new();
    static mut FREE: Vec<u32> = Vec::new();

    fn slot(handle: NonZeroU32) -> &'static mut Slot {
        // SAFE: A procedural macro runs single-threaded, and the stream is !Send
        unsafe { SLOTS[handle.get() as usize - 1].as_mut().expect("dead token stream handle") }
    }

    impl TokenStream {
        fn allocate(trees: Vec<crate::TokenTree>) -> NonZeroU32 {
            let new = Slot { trees, handles: 1 };
            // SAFE: See `slot`
            unsafe {
                if let Some(index) = FREE.pop() {
                    SLOTS[index as usize] = Some(new);
                    NonZeroU32::new(index + 1).expect("stream handle")
                }
                else {
                    SLOTS.push(Some(new));
                    NonZeroU32::new(SLOTS.len() as u32).expect("stream handle")
                }
            }
        }
        pub(crate) fn from_trees(trees: Vec<crate::TokenTree>) -> TokenStream {
            if trees.is_empty() {
                return TokenStream(None);
            }
            TokenStream(Some(TokenStream::allocate(trees)))
        }
        pub(crate) fn trees(&self) -> &[crate::TokenTree] {
            match self.0 {
            Some(handle) => &slot(handle).trees,
            None => &[],
            }
        }
        pub(crate) fn trees_mut(&mut self) -> &mut Vec<crate::TokenTree> {
            match self.0 {
            Some(handle) if slot(handle).handles == 1 => {},
            Some(handle) => {
                let copy = slot(handle).trees.clone();
                *self = TokenStream(Some(TokenStream::allocate(copy)));
                },
            None => {
                self.0 = Some(TokenStream::allocate(Vec::new()));
                },
            }
            &mut slot(self.0.expect("stream handle")).trees
        }
        pub(crate) fn into_trees(self) -> Vec<crate::TokenTree> {
            match self.0 {
            Some(handle) if slot(handle).handles == 1 => ::std::mem::take(&mut slot(handle).trees),
            Some(_) => self.trees().to_vec(),
            None => Vec::new(),
            }
        }
    }

    impl Clone for TokenStream {
        fn clone(&self) -> TokenStream {
            if let Some(handle) = self.0 {
                slot(handle).handles += 1;
            }
            TokenStream(self.0)
        }
    }
    impl Drop for TokenStream {
        fn drop(&mut self) {
            if let Some(handle) = self.0 {
                let entry = slot(handle);
                entry.handles -= 1;
                if entry.handles == 0 {
                    // SAFE: See `slot`
                    unsafe {
                        SLOTS[handle.get() as usize - 1] = None;
                        FREE.push(handle.get() - 1);
                    }
                }
            }
        }
    }
    impl Default for TokenStream {
        fn default() -> TokenStream {
            TokenStream(None)
        }
    }
    impl ::std::fmt::Debug for TokenStream {
        fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
            f.write_str("TokenStream ")?;
            f.debug_list().entries(self.trees()).finish()
        }
    }

    #[derive(Clone)]
    pub struct IntoIter {
        it: ::std::vec::IntoIter<crate::TokenTree>,
    }
    impl Iterator for IntoIter {
        type Item = crate::TokenTree;
        fn next(&mut self) -> Option<crate::TokenTree> {
            self.it.next()
        }
    }

    impl TokenStream {
        // 1.29
        pub fn new() -> TokenStream {
            TokenStream(None)
        }
        // 1.29
        pub fn is_empty(&self) -> bool {
            self.trees().is_empty()
        }
    }

    // 1.29
    impl IntoIterator for TokenStream
    {
        type Item = super::TokenTree;
        type IntoIter = IntoIter;
        fn into_iter(self) -> IntoIter {
            IntoIter {
                it: self.into_trees().into_iter(),
            }
        }
    }

    impl ::std::fmt::Display for TokenStream
    {
        fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
            let mut joint = false;
            for (i, token) in self.trees().iter().enumerate() {
                if i != 0 && !joint {
                    f.write_str(" ")?;
                }
                joint = false;
                match token {
                &crate::TokenTree::Group(ref group) => {
                    let (open, close) = match group.delimiter() {
                        crate::Delimiter::Parenthesis => ("(", ")"),
                        crate::Delimiter::Brace => ("{", "}"),
                        crate::Delimiter::Bracket => ("[", "]"),
                        crate::Delimiter::None => ("", ""),
                        };
                    let stream = group.stream();
                    if stream.is_empty() {
                        write!(f, "{} {}", open, close)?;
                    }
                    else {
                        write!(f, "{} {} {}", open, stream, close)?;
                    }
                    },
                &crate::TokenTree::Ident(ref ident) => write!(f, "{}", ident)?,
                &crate::TokenTree::Punct(ref punct) => {
                    write!(f, "{}", punct.as_char())?;
                    joint = punct.spacing() == crate::Spacing::Joint;
                    },
                &crate::TokenTree::Literal(ref literal) => write!(f, "{}", literal)?,
                }
            }
            Ok(())
        }
    }

    // 1.29.0
    impl From<crate::TokenTree> for TokenStream
    {
        fn from(t: crate::TokenTree) -> TokenStream {
            TokenStream::from_trees(vec![t])
        }
    }

    impl ::std::iter::FromIterator<TokenStream> for TokenStream
    {
        fn from_iter<I: IntoIterator<Item = TokenStream>>(streams: I) -> Self
        {
            let mut rv = TokenStream::new();
            rv.extend(streams);
            rv
        }
    }
    impl ::std::iter::FromIterator<crate::TokenTree> for TokenStream
    {
        fn from_iter<I: IntoIterator<Item = crate::TokenTree>>(tokens: I) -> Self
        {
            TokenStream::from_trees(tokens.into_iter().collect())
        }
    }

    // 1.30
    impl ::std::iter::Extend<TokenStream> for TokenStream
    {
        fn extend<I: IntoIterator<Item = TokenStream>>(&mut self, streams: I) {
            for stream in streams {
                if stream.is_empty() {
                    continue;
                }
                if self.is_empty() {
                    *self = stream;
                    continue;
                }
                let trees = stream.into_trees();
                self.trees_mut().extend(trees);
            }
        }
    }
    // 1.30
    impl ::std::iter::Extend<crate::TokenTree> for TokenStream
    {
        fn extend<I: IntoIterator<Item = crate::TokenTree>>(&mut self, trees: I)
        {
            let mut trees = trees.into_iter().peekable();
            if trees.peek().is_some() {
                self.trees_mut().extend(trees);
            }
        }
    }
}

pub use crate::span::Span;

pub use crate::token_stream::TokenStream;

pub use crate::token_tree::{TokenTree,Group,Ident,Punct,Literal};
pub use crate::token_tree::{Delimiter,Spacing};

pub use crate::lex::LexError;
pub use crate::diagnostic::{Diagnostic,Level,MultiSpan};


#[doc(hidden)]
pub enum MacroType {
    SingleStream(fn(TokenStream)->TokenStream),
    Attribute(fn(TokenStream,TokenStream)->TokenStream),
}
#[doc(hidden)]
pub struct MacroDesc
{
    name: &'static str,
    handler: MacroType,
}

static mut IS_AVAILABLE: bool = false;
#[doc(hidden)]
pub fn main(macros: &[MacroDesc])
{
    // SAFE: This is the entrypoint, so no threads running yet
    unsafe {
        IS_AVAILABLE = true;
    }
    //::env_logger::init();

    let mut args = ::std::env::args();
    let _ = args.next().expect("Should have an executable name");
    let mac_name = args.next().expect("Was not passed a macro name");
    let input_path = args.next();
    //eprintln!("Searching for macro {}\r", mac_name);
    for m in macros
    {
        if m.name == mac_name {
            use std::io::Write;
            ::std::io::stdout().write(&[0]).expect("Stdout write error?");
            ::std::io::stdout().flush().expect("Stdout write error?");
            debug!("Waiting for input\r");
            let mut stdin_raw;
            let mut fp_raw;
            let stdin = if let Some(p) = input_path {
                    fp_raw = ::std::fs::File::open(p).unwrap();
                    &mut fp_raw as &mut /*dyn */::std::io::Read
                }
                else {
                    stdin_raw = ::std::io::stdin().lock();
                    &mut stdin_raw
                };
            let input = crate::serialisation::recv_token_stream(stdin);
            debug!("INPUT = `{}`\r", input);
            let output = match m.handler
                {
                MacroType::SingleStream(h) => {
                    Span::freeze_definitions();
                    (h)(input)
                    },
                MacroType::Attribute(h) => {
                    let input_body = crate::serialisation::recv_token_stream(stdin);
                    debug!("INPUT BODY = `{}`\r", input_body);
                    Span::freeze_definitions();
                    (h)(input, input_body)
                    },
                };
            debug!("OUTPUT = `{}`\r", output);
            let stdout = ::std::io::stdout();
            crate::serialisation::send_token_stream(stdout.lock(), output);
            ::std::io::Write::flush(&mut ::std::io::stdout()).expect("Stdout write error?");
            note!("Done");
            return ;
        }
    }
    panic!("Unknown macro name '{}'", mac_name);
}

pub fn is_available() -> bool {
    // SAFE: Reading from a value only ever written in single-threaded code
    unsafe { IS_AVAILABLE }
}
