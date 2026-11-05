#![feature(decl_macro)]

pub mod home {
    pub mod nested {
        pub macro value() { 631u16 }
    }
    pub macro import_value($alias:ident) {
        use nested::value as $alias;
    }
    pub macro import_cfg($alias:ident) {
        use std::cfg as $alias;
    }
}
