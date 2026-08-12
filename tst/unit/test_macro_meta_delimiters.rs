macro_rules! take_meta {
    ($meta:meta) => {};
}

take_meta!(name(words inside));
take_meta!(name[words inside]);
take_meta!(name { words inside });
take_meta!(name);
take_meta!(name = 0);

fn main() {}
