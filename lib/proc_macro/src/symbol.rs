//! Interned strings, as upstream's bridge client keeps them: a token's text
//! is a small handle, so `Ident` and `Literal` have upstream's sizes.

use ::std::num::NonZeroU32;

#[derive(Copy,Clone,PartialEq,Eq,Hash)]
pub(crate) struct Symbol(NonZeroU32);

static mut NAMES: Vec<&'static str> = Vec::new();
static mut INDEX: Option<::std::collections::HashMap<&'static str, Symbol>> = None;

impl Symbol
{
    pub(crate) fn intern(text: &str) -> Symbol {
        // SAFE: A procedural macro runs single-threaded, and every token type is !Send
        unsafe {
            let index = INDEX.get_or_insert_with(::std::collections::HashMap::new);
            if let Some(&symbol) = index.get(text) {
                return symbol;
            }
            let name: &'static str = Box::leak(text.to_owned().into_boxed_str());
            NAMES.push(name);
            let symbol = Symbol(NonZeroU32::new(NAMES.len() as u32).expect("symbol table overflow"));
            index.insert(name, symbol);
            symbol
        }
    }
    pub(crate) fn as_str(self) -> &'static str {
        // SAFE: See `intern`
        unsafe { NAMES[self.0.get() as usize - 1] }
    }
}
impl ::std::fmt::Debug for Symbol
{
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        ::std::fmt::Debug::fmt(self.as_str(), f)
    }
}
