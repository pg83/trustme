// icu_locale_core's `enum_keyword!` captures `$([$derive_attrs:ty])?` and
// `$([$variant_attr:ty])?` and writes `#[derive($derive_attrs)]` and
// `#[$variant_attr]`. Upstream parses a path from a `ty` fragment that is a
// path by reparsing it as one (`parse_path_inner`: `MetaVarKind::Ty {
// is_path: true }`), so the derive names `Default` and the variant's
// attribute is `#[default]`.
macro_rules! keyword {
    ($([$derive_attrs:ty])? $name:ident) => {
        $(#[derive($derive_attrs)])?
        #[derive(Debug, PartialEq)]
        struct $name(u8);
    };
}

keyword!([Default] Plain);

macro_rules! choice {
    ($([$derive_attrs:ty])? $name:ident { $($([$variant_attr:ty])? $variant:ident),* }) => {
        $(#[derive($derive_attrs)])?
        #[derive(Debug, PartialEq)]
        enum $name {
            $($(#[$variant_attr])? $variant),*
        }
    };
}

choice!([Default] Mode { A, [default] B, C });

macro_rules! call {
    ($t:ty) => {
        <$t>::new()
    };
}

fn main() {
    assert_eq!(Plain::default(), Plain(0));
    assert_eq!(Mode::default(), Mode::B);
    assert_ne!(Mode::A, Mode::C);
    let v: Vec<u8> = call!(Vec<u8>);
    assert!(v.is_empty());
}
