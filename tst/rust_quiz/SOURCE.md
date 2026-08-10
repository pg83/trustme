# Rust Quiz corpus

Source: <https://github.com/dtolnay/rust-quiz/tree/23e3039413d2ea15db9f6edcfe96851e8b76c03f/questions>

Revision: commit `23e3039413d2ea15db9f6edcfe96851e8b76c03f`.

Each executable question is stored as an individual `.rs` file with its
expected output in the adjacent `.stdout` file.  Tombstoned questions and the
one question whose answer is a compile error are not runtime cases.

Refresh from a temporary checkout with:

```sh
tst/rust_quiz/import.py /tmp/rust-quiz
```
