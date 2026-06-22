fn main() {
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    let config = cbindgen::Config::from_file("cbindgen.toml")
        .expect("cbindgen.toml not found");
    cbindgen::Builder::new()
        .with_crate(&crate_dir)
        .with_config(config)
        .generate()
        .expect("cbindgen failed")
        .write_to_file("../../src/ffi/ffi_auto.h");

    check_abi_sizes();
}

// sizeof_check.cpp (C++) ve metrics.rs (Rust) arasindaki ABI buyukluklerini karsilastir.
// Her iki tarafta da deger varsa uyusmuyorsa derleme hata verir.
fn check_abi_sizes() {
    let manifest = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    let workspace = std::path::Path::new(&manifest)
        .parent().unwrap()  // orchestrator -> src
        .parent().unwrap(); // src          -> repo root

    let cpp_src = match std::fs::read_to_string(workspace.join("src/ffi/sizeof_check.cpp")) {
        Ok(s)  => s,
        Err(_) => return, // CI ortaminda dosya yoksa atla
    };
    let rust_src = match std::fs::read_to_string(workspace.join("src/orchestrator/src/metrics.rs")) {
        Ok(s)  => s,
        Err(_) => return,
    };
    let ffi_src = match std::fs::read_to_string(workspace.join("src/orchestrator/src/ffi.rs")) {
        Ok(s)  => s,
        Err(_) => return,
    };

    let cpp_sizes  = parse_cpp_static_asserts(&cpp_src);
    let mut rust_sizes = parse_rust_size_asserts(&rust_src);
    let ffi_sizes = parse_rust_size_asserts(&ffi_src);
    rust_sizes.extend(ffi_sizes);

    // Rust adi -> C++ adi eslesmeleri
    let name_map: &[(&str, &str)] = &[
        ("MetricSample", "RjMetricSample"),
        ("RjAction",     "RjAction"),      // E1: ABI boyut kontrolü
        ("RjCommand",    "RjCommand"),     // E1: ABI boyut kontrolü
    ];

    for (rust_name, cpp_name) in name_map {
        let r = rust_sizes.get(*rust_name);
        let c = cpp_sizes.get(*cpp_name);
        match (r, c) {
            (Some(rv), Some(cv)) if rv == cv => {
                println!("cargo:warning=ABI OK: {} = {} bytes", cpp_name, rv);
            }
            (Some(rv), Some(cv)) => {
                panic!(
                    "ABI UYUMSUZLUGU: {} — Rust={} bytes, C++={} bytes. \
                     sizeof_check.cpp veya metrics.rs guncellenmeli.",
                    cpp_name, rv, cv
                );
            }
            _ => { /* bir tarafta tanimsiz — atla */ }
        }
    }
}

// static_assert(sizeof(TypeName) == N, ...) satirlarini ayristirir
fn parse_cpp_static_asserts(src: &str) -> std::collections::HashMap<String, usize> {
    let mut map = std::collections::HashMap::new();
    for line in src.lines() {
        let t = line.trim();
        if !t.starts_with("static_assert(sizeof(") { continue; }
        let prefix_len = "static_assert(sizeof(".len();
        let after = &t[prefix_len..];
        if let Some(close) = after.find(')') {
            let type_name = after[..close].to_string();
            if let Some(eq) = t.find("== ") {
                let rest = &t[eq + 3..];
                let n: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
                if let Ok(size) = n.parse::<usize>() {
                    map.insert(type_name, size);
                }
            }
        }
    }
    map
}

// assert!(core::mem::size_of::<TypeName>() == N) satirlarini ayristirir
fn parse_rust_size_asserts(src: &str) -> std::collections::HashMap<String, usize> {
    let mut map = std::collections::HashMap::new();
    let key = "size_of::<";
    for line in src.lines() {
        let t = line.trim();
        if let Some(s) = t.find(key) {
            let after = &t[s + key.len()..];
            if let Some(e) = after.find(">()") {
                let type_name = after[..e].to_string();
                if let Some(eq) = t.find("== ") {
                    let rest = &t[eq + 3..];
                    let n: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
                    if let Ok(size) = n.parse::<usize>() {
                        map.insert(type_name, size);
                    }
                }
            }
        }
    }
    map
}
