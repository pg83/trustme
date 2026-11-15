//@ output-contains: .cpp __asm__ (".att_syntax prefix; .set .Lcov99_w1090_att, 1090;
//@ output-contains: .cpp .intel_syntax noprefix; ");

core::arch::global_asm!(".set .Lcov99_w1090_att, 1090", options(att_syntax));

fn main() {}
