#![feature(decl_macro)]

pub mod source {
    pub const LIMIT: u16 = 31;
    pub static BIAS: u16 = 7;
    pub fn scale(value: u16) -> u16 { value * 2 }
    pub type Word = u16;
    pub struct Unit;
    pub struct Pair(pub u16);
    pub enum Entry { Unit, Data(u16) }
    pub union Bits { pub value: u16, pub bytes: [u8; 2] }
    pub trait Read { fn read(&self) -> u16; }
    impl Read for Pair { fn read(&self) -> u16 { self.0 } }

    pub macro values() { (LIMIT, BIAS, scale(11)) }
    pub macro types() {{
        let word: Word = 11;
        let _unit = Unit;
        let pair = Pair(13);
        let entry = Entry::Data(17);
        let bits = Bits { value: 19 };
        let payload = match entry { Entry::Data(value) => value, Entry::Unit => 0 };
        (word, pair.0, payload, unsafe { bits.value }, <Pair as Read>::read(&pair))
    }}
}

pub use source::{values, types, Entry, LIMIT as IMPORTED_CONST, Unit as IMPORTED_UNIT,
    Pair as IMPORTED_PAIR, Word as IMPORTED_TYPE, Read as IMPORTED_TRAIT};
pub use source::Entry::Data as IMPORTED_VARIANT;
pub use namespace_foundation::{remote as IMPORTED_MODULE, RemotePair as REMOTE_PAIR};

pub macro imports() {{
    let word: IMPORTED_TYPE = IMPORTED_CONST;
    let _unit = IMPORTED_UNIT;
    let pair = IMPORTED_PAIR(word);
    let payload = match IMPORTED_VARIANT(23) { IMPORTED_VARIANT(value) => value, Entry::Unit => 0 };
    (word, <IMPORTED_PAIR as IMPORTED_TRAIT>::read(&pair), payload)
}}

pub macro remote_imports() {{
    let pair = REMOTE_PAIR(43);
    (IMPORTED_MODULE::value(), pair.0)
}}
