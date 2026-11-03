//@ proc-macro-aux-build: doc_token_observer.rs
use doc_token_observer::observed_docs;

#[observed_docs]
/// outer orchard
mod garden {
    //! inner lantern
    pub struct Plant {
        /** field river */
        pub value: u32,
    }
    impl Plant {
        /// method mountain
        pub fn doubled(&self) -> u32 { self.value * 2 }
    }
}

fn main() {
    assert_eq!(garden::Plant { value: 17 }.doubled(), 34);
}
