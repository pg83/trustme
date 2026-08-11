pub fn arithmetic(left: i32, right: i32) -> [i32; 7] {
    [
        left + right,
        left - right,
        left * right,
        left / right,
        left % right,
        left << right,
        left >> right,
    ]
}
