import sys
sys.stdout.reconfigure(encoding='utf-8')

import numpy as np
import pandas as pd
import warnings
from sklearn.tree import DecisionTreeClassifier
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import classification_report, confusion_matrix
import tensorflow as tf
from tensorflow import keras
warnings.filterwarnings('ignore')

# ─── CONFIG ──────────────────────────────────────────────────────
N_SAMPLES   = 128
SAMPLE_RATE = 500
N_CLASSES   = 4
CLASS_NAMES = ['normal', 'blocked', 'unbalanced', 'off']
# ─────────────────────────────────────────────────────────────────


# ════════════════════════════════════════════════════════════════
#  FEATURE EXTRACTION  —  must match ESP32 exactly
# ════════════════════════════════════════════════════════════════

def extract_fft_features(raw_window, sample_rate=SAMPLE_RATE):
    n          = len(raw_window)
    fft_vals   = np.fft.rfft(raw_window)
    magnitudes = np.abs(fft_vals)[:n // 2]
    dominant_bin  = np.argmax(magnitudes[1:]) + 1   # skip DC bin 0
    dominant_freq = dominant_bin * (sample_rate / n)
    max_amplitude = np.max(magnitudes[1:])
    mean_energy   = np.mean(magnitudes[1:])
    return [dominant_freq, max_amplitude, mean_energy]


# ════════════════════════════════════════════════════════════════
#  LOAD DATA
# ════════════════════════════════════════════════════════════════

def load_and_extract(csv_path="fan_dataset.csv"):
    print(f"Loading: {csv_path}")
    df = pd.read_csv(csv_path)

    print(f"\nClass distribution:")
    for label, count in df['label'].value_counts().sort_index().items():
        print(f"  Class {int(label)} ({CLASS_NAMES[int(label)]}): {count} windows")

    X, y = [], []
    for _, row in df.iterrows():
        raw      = row[[f's{i}' for i in range(N_SAMPLES)]].values.astype(float)
        features = extract_fft_features(raw)
        X.append(features)
        y.append(int(row['label']))

    X = np.array(X)
    y = np.array(y)
    print(f"\nFeature matrix shape: {X.shape}")
    print(f"Features: [dominant_freq, max_amplitude, mean_energy]")
    return X, y


# ════════════════════════════════════════════════════════════════
#  MODEL 1: DECISION TREE
# ════════════════════════════════════════════════════════════════

def generate_dt_manual(clf):
    from sklearn.tree import _tree
    tree  = clf.tree_
    lines = []
    lines.append("// Auto-generated Decision Tree")
    lines.append("// Classes: 0=normal  1=blocked  2=unbalanced  3=off")
    lines.append("")
    lines.append("int dt_predict(float dom_freq, float max_amp, float mean_energy) {")
    lines.append("  float f[3] = {dom_freq, max_amp, mean_energy};")

    def recurse(node, depth):
        indent = "  " * (depth + 1)
        if tree.feature[node] != _tree.TREE_UNDEFINED:
            lines.append(f"{indent}if (f[{tree.feature[node]}] <= {tree.threshold[node]:.6f}f) {{")
            recurse(tree.children_left[node],  depth + 1)
            lines.append(f"{indent}}} else {{")
            recurse(tree.children_right[node], depth + 1)
            lines.append(f"{indent}}}")
        else:
            pred = int(np.argmax(tree.value[node]))
            lines.append(f"{indent}return {pred};  // {CLASS_NAMES[pred]}")

    recurse(0, 0)
    lines.append("}")

    with open("dt_model.h", 'w') as f:
        f.write('\n'.join(lines))
    print("Saved: dt_model.h (manual)")


def train_decision_tree(X_train, X_test, y_train, y_test):
    print("\n" + "=" * 50)
    print("  MODEL 1: Decision Tree")
    print("=" * 50)

    clf = DecisionTreeClassifier(max_depth=6, random_state=42)
    clf.fit(X_train, y_train)
    y_pred = clf.predict(X_test)
    acc    = (y_pred == y_test).mean()

    print(f"\nAccuracy: {acc * 100:.1f}%\n")
    print(classification_report(y_test, y_pred, target_names=CLASS_NAMES))
    print("Confusion Matrix (rows=actual, cols=predicted):")
    print(pd.DataFrame(
        confusion_matrix(y_test, y_pred),
        index=CLASS_NAMES, columns=CLASS_NAMES
    ), "\n")

    try:
        from micromlgen import port
        c_code = port(clf, classmap={0:'normal',1:'blocked',2:'unbalanced',3:'off'})
        with open("dt_model.h", 'w') as f:
            f.write(c_code)
        print("Saved: dt_model.h (micromlgen)")
    except ImportError:
        generate_dt_manual(clf)
    return acc


# ════════════════════════════════════════════════════════════════
#  MODEL 2: RANDOM FOREST
# ════════════════════════════════════════════════════════════════

def train_random_forest(X_train, X_test, y_train, y_test):
    print("\n" + "=" * 50)
    print("  MODEL 2: Random Forest")
    print("=" * 50)

    clf = RandomForestClassifier(n_estimators=15, max_depth=6, random_state=42)
    clf.fit(X_train, y_train)
    y_pred = clf.predict(X_test)
    acc    = (y_pred == y_test).mean()

    print(f"\nAccuracy: {acc * 100:.1f}%\n")
    print(classification_report(y_test, y_pred, target_names=CLASS_NAMES))
    print("Confusion Matrix (rows=actual, cols=predicted):")
    print(pd.DataFrame(
        confusion_matrix(y_test, y_pred),
        index=CLASS_NAMES, columns=CLASS_NAMES
    ), "\n")

    try:
        from micromlgen import port
        c_code = port(clf, classmap={0:'normal',1:'blocked',2:'unbalanced',3:'off'})
        with open("rf_model.h", 'w') as f:
            f.write(c_code)
        print("Saved: rf_model.h (micromlgen)")
    except ImportError:
        print("micromlgen not found. Install: pip install micromlgen==1.1.99")
    return acc


# ════════════════════════════════════════════════════════════════
#  MODEL 3: NEURAL NETWORK
# ════════════════════════════════════════════════════════════════

def export_nn_weights(model, scaler):
    print("Exporting neural network weights...")
    lines = []
    lines.append("// Auto-generated Neural Network weights for ESP32")
    lines.append("// Architecture: 3 -> 16 -> 8 -> 4")
    lines.append("// Classes: 0=normal  1=blocked  2=unbalanced  3=off")
    lines.append("")

    mean_str = ", ".join([f"{v:.8f}f" for v in scaler.mean_])
    std_str  = ", ".join([f"{v:.8f}f" for v in scaler.scale_])
    lines.append("// StandardScaler parameters")
    lines.append(f"const float SCALER_MEAN[3] = {{{mean_str}}};")
    lines.append(f"const float SCALER_STD[3]  = {{{std_str}}};")
    lines.append("")

    dense_layers = [l for l in model.layers if len(l.get_weights()) == 2]
    layer_sizes  = [3]

    for idx, layer in enumerate(dense_layers):
        W, b     = layer.get_weights()
        in_size, out_size = W.shape
        layer_sizes.append(out_size)
        lines.append(f"// Layer {idx+1}: {in_size} -> {out_size}")
        lines.append(f"const float W{idx+1}[{in_size}][{out_size}] = {{")
        for row in W:
            lines.append("  {" + ", ".join([f"{v:.8f}f" for v in row]) + "},")
        lines.append("};")
        b_str = ", ".join([f"{v:.8f}f" for v in b])
        lines.append(f"const float b{idx+1}[{out_size}] = {{{b_str}}};")
        lines.append("")

    for i, size in enumerate(layer_sizes):
        lines.append(f"#define NN_LAYER_{i}_SIZE {size}")
    lines.append("")
    lines.append("inline float relu(float x) { return x > 0 ? x : 0; }")

    with open("nn_model.h", 'w') as f:
        f.write('\n'.join(lines))
    print("Saved: nn_model.h")


def train_neural_network(X_train, X_test, y_train, y_test):
    print("\n" + "=" * 50)
    print("  MODEL 3: Neural Network  3->16->8->4")
    print("=" * 50)

    scaler     = StandardScaler()
    X_train_s  = scaler.fit_transform(X_train)
    X_test_s   = scaler.transform(X_test)
    y_train_oh = keras.utils.to_categorical(y_train, N_CLASSES)
    y_test_oh  = keras.utils.to_categorical(y_test,  N_CLASSES)

    model = keras.Sequential([
        keras.layers.Input(shape=(3,)),
        keras.layers.Dense(16, activation='relu'),
        keras.layers.Dense(8,  activation='relu'),
        keras.layers.Dense(N_CLASSES, activation='softmax'),
    ])
    model.compile(
        optimizer='adam',
        loss='categorical_crossentropy',
        metrics=['accuracy']
    )

    print("Training (150 epochs)...")
    model.fit(
        X_train_s, y_train_oh,
        epochs=150,
        batch_size=16,
        validation_split=0.15,
        verbose=0
    )

    _, acc = model.evaluate(X_test_s, y_test_oh, verbose=0)
    y_pred = np.argmax(model.predict(X_test_s, verbose=0), axis=1)

    print(f"\nAccuracy: {acc * 100:.1f}%\n")
    print(classification_report(y_test, y_pred, target_names=CLASS_NAMES))
    print("Confusion Matrix (rows=actual, cols=predicted):")
    print(pd.DataFrame(
        confusion_matrix(y_test, y_pred),
        index=CLASS_NAMES, columns=CLASS_NAMES
    ), "\n")

    export_nn_weights(model, scaler)
    return acc


# ════════════════════════════════════════════════════════════════
#  MAIN
# ════════════════════════════════════════════════════════════════

def main():
    print("=" * 50)
    print("  Fanalyzer - Model Training Pipeline")
    print("  normal / blocked / unbalanced / off")
    print("=" * 50)

    X, y = load_and_extract()

    missing = [CLASS_NAMES[i] for i in range(N_CLASSES) if i not in np.unique(y)]
    if missing:
        print(f"\nERROR: Missing classes: {missing}")
        print("Run collect_data.py to collect missing classes first.")
        return

    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42, stratify=y
    )
    print(f"\nTrain: {len(X_train)} samples  |  Test: {len(X_test)} samples")

    acc_dt = train_decision_tree(X_train, X_test, y_train, y_test)
    acc_rf = train_random_forest(X_train, X_test, y_train, y_test)
    acc_nn = train_neural_network(X_train, X_test, y_train, y_test)

    print("=" * 50)
    print("  FINAL RESULTS")
    print("=" * 50)
    results = [
        ("Decision Tree",  acc_dt),
        ("Random Forest",  acc_rf),
        ("Neural Network", acc_nn),
    ]
    for name, acc in results:
        bar = "#" * int(acc * 20)
        print(f"  {name:<16} {acc*100:5.1f}%  [{bar:<20}]")

    best = max(results, key=lambda x: x[1])
    print(f"\n  Best model: {best[0]} ({best[1]*100:.1f}%)")
    print("\nGenerated files:")
    print("  dt_model.h  ->  copy into  fanalyzer_decision_tree/")
    print("  rf_model.h  ->  copy into  fanalyzer_random_forest/")
    print("  nn_model.h  ->  copy into  fanalyzer_neural_network/")


if __name__ == "__main__":
    main()