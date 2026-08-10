// Extracted from src/tokens.md:538
#![allow(unused)]
fn main() {
    0b0102;  // This is not `0b010` followed by `2`.
    0o1279;  // This is not `0o127` followed by `9`.
    0x80.0;  // This is not `0x80` followed by `.` and `0`.
    0b101e;  // This is not a suffixed literal or `0b101` followed by `e`.
    0b;      // This is not an integer literal or `0` followed by `b`.
    0b_;     // This is not an integer literal or `0` followed by `b_`.
    2em;     // This is not a suffixed literal or `2` followed by `em`.
    2.0em;   // This is not a suffixed literal or `2.0` followed by `em`.
}
