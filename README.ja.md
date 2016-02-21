# mod_barrier
既知のクローラーの検証を行うためのApacheモジュール

【重要】このモジュールは非常に初期のリリースです。十分なテストはまだ行っていません。
実環境で使用する際には十分にご注意の上、ご利用ください。

## 必須要件

* 最新版のCentOS 7で稼働するApache HTTPD 2.4。他のプラットフォーム、バージョンではテストは行えていません。
* 適切なmod_socache_xxxxモジュール。通常はデフォルトのインストールから何も行う必要はありません。socacheについては https://httpd.apache.org/docs/2.4/en/socache.html を参照してください。

他のプラットフォームのビルドを行うには Makefile、module.mk を修正する必要があります。

このモジュールは古いApache HTTPD 2.4でも動作すると思いますが、 HTTPD 2.2で動作するかはよくわかりません。（多分無理だと思います。）

## ビルド

コマンドラインで `make` してください。

## インストール

`sudo make install` でインストールします。

インストール、設定完了後はhttpdを再起動してください。

## 設定

下記のサンプルファイルは最小限の構成です。

~~~
LoadModule  barrier_module  modules/mod_barrier.so

# https://support.google.com/webmasters/answer/80553?hl=en
BrowserMatch Googlebot UA_BARRIER_CHECK=.googlebot.com,.google.com

# https://www.bing.com/webmaster/help/how-to-verify-bingbot-3905dc26
BrowserMatch bingbot   UA_BARRIER_CHECK=.search.msn.com

# http://www.ysearchblog.com/2007/06/05/yahoo-search-crawler-slurp-has-a-new-address-and-signature-card/
BrowserMatch 'Yahoo! Slurp' UA_BARRIER_CHECK=.crawl.yahoo.net

BarrierBlockExpire  20
BarrierSOCache      shmcb
~~~

### 設定

#### BrowserMatch
これはApacheで使用されるBrowserMatchです。このモジュールは UA_BARRIER_CHECK 変数を使用します。
この変数名は今のところ変更できません。複数のドメインをカンマ区切りで設定することができます。ドメインは次の2種類を指定することができます。

 - 部分一致(後方一致): 先頭にドットを含めて記載することで、ドメイン名に一致するように指定することができます。
 - 完全一致: 先頭にドットがない場合には完全一致になります。

BrowserMatch については https://httpd.apache.org/docs/2.4/mod/mod_setenvif.html を参照してください。

#### BarrierBlockExpire
ドメインの検査結果をこのディレクティブで指定した秒数、キャッシュします。

#### BarrierSOCache
キャッシュメカニズム。キャッシュを指定するにはこのディレクティブを指定します。通常は `shmcb` を指定することで良いと思います。キャッシュメカニズムの詳細は https://httpd.apache.org/docs/2.4/en/socache.html　を参照してください。

