trait Execute { type Result; fn execute(self, n: u16) -> Self::Result; }
impl Execute for fn(u16) -> u32 { type Result = u32; fn execute(self, n: u16) -> u32 { self(n) } }
fn execute<F: Execute<Result = u32>>(fs: [F; 1], n: u16) -> u32 {
    let [f] = fs;
    f.execute(n)
}
fn square(n: u16) -> u32 { n as u32 * n as u32 }
fn main() { let fs = [square as fn(u16) -> u32]; assert_eq!(execute(fs, 13), 169); }
