1. Firebase Consoleへ移動
2. プロジェクトを作成
3. Google Analyticsは必要になるまで無効でよい
4. Realtime Databaseを作成
   - 左上3本線 > Database と Storage > Realtime Database
5. リージョンを選択
   - 地理的に近いところ
6. 初期Security Rulesを設定
   - ロックモード
7. Authenticationを有効化
   - 左メニュー > セキュリティ > Authentication
   - 始める
   - ログイン方法 / Sign-in method
   - メール/パスワードを有効化して保存
8. ユーザーを追加
   - メール/パスワードを入力して保存
9. Realtime Databaseに /wake/request を追加
   - ＋ボタンを押す
   - キー: wake
   - wake の右側の ＋ を押す
   - キー: request
   - 値: 1
   - 追加
10. Realtime Databaseのルールを変更して公開

	```
	{  
	  "rules": {  
	    ".read": false,  
	    ".write": false,  
	    "wake": {  
	      "request": {  
	        ".read": "auth != null",  
	        ".write": "auth != null",  
	        ".validate": "newData.isBoolean() || (newData.isNumber() && (newData.val() == 0 || newData.val() == 1))"  
	      }  
	    }  
	  }  
	}
	```
11. API_KEY を控える
　- 左メニュー > プロジェクトの概要
　- アプリを追加 > `</>ウェブ` を選択 > アプリ名を入れて登録
　- apiKeyを控える
	　- 登録済みの場合
	　- 左メニュー > プロジェクトの設定 > 全般
	　- マイアプリに表示される。