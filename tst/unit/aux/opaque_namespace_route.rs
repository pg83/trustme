#![feature(decl_macro)]

pub mod home {
    pub mod nested {
        pub macro value() { 607u16 }
    }

    pub macro route($name:ident) {
        use nested as $name;
    }
}
