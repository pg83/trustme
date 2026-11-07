macro_rules! declare {
    ($condition:meta, $word:tt) => {
        #[cfg_attr($condition, inline)]
        #[doc = concat!("singleton")]
        fn label() -> &'static str { stringify!($word) }
    };
}

declare!(all(), aurora);

macro_rules! attributed {
    ($attribute:meta) => {
        #[$attribute]
        fn number() -> u16 { 53 }
    };
}

attributed!(inline);

fn main() {
    assert_eq!(label(), "aurora");
    assert_eq!(number(), 53);
    assert_eq!(concat!("lumen"), "lumen");
    assert!(cfg!(all()));
    assert!(!cfg!(any()));
}
