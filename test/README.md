# 使い方
## 初回だけ
```
// pytestのインストール
pip install pytest
```

## テスト方法
```
// コンパイル
gcc soft/soft.c
// test
pytest -s -p no:warnings test/main.py 
```

26行目の第一引数は乱数シード値、第二引数はテスト数