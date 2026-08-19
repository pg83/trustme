//@ run-pass
// `stringify!` of a `meta` fragment prints the attribute it holds, and a doc
// comment's string prints as the raw literal rustc writes it as, with just
// enough hashes to close it.

macro_rules! meta {
    (#[$y:meta]) => {
        stringify!($y)
    };
}

macro_rules! trees {
    ($($x:tt)*) => {
        stringify!($($x)*)
    };
}

fn main() {
    assert_eq!(meta!(#[doc = "x"]), "doc = \"x\"");
    assert_eq!(meta!(#[cfg(unix)]), "cfg(unix)");
    assert_eq!(meta!(#[allow(dead_code, unused)]), "allow(dead_code, unused)");
    assert_eq!(meta!(#[repr(C)]), "repr(C)");
    assert_eq!(meta!(/// Madoka
                     ), "doc = r\" Madoka\"");
    assert_eq!(meta!(/// One quote mark: ["]
                     ), "doc = r#\" One quote mark: [\"]\"#");
    assert_eq!(meta!(/// Raw string ending sequences: ["###]
                     ), "doc = r####\" Raw string ending sequences: [\"###]\"####");
    assert_eq!(trees!(/// Madoka
                      ), "#[doc = r\" Madoka\"]");
    assert_eq!(trees!(#[doc = "x"]), "#[doc = \"x\"]");
}
