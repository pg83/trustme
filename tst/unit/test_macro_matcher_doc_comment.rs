macro_rules! ignores_doc_comment {
    (
        /// ignored
    ) => {};
}

fn main() {
    ignores_doc_comment!();
}
