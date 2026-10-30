/* SPDX-License-Identifier: Apache-2.0
 * 中文：用真实WKWebView检查微信读书手机页；全部账户与网络为合成夹具，不访问设备。
 * English: Check the real mobile WeRead page in WKWebView with synthetic accounts/network and no devices.
 */
import AppKit
import Foundation
import WebKit
private enum TestError:Error {case failed(String)}
@MainActor final class WeReadWebTest:NSObject,WKNavigationDelegate {
 let view:WKWebView;let window:NSWindow;let output:URL;let origin:String;var pending:CheckedContinuation<Void,Error>?;var checks=0;var screenshots=0
 init(output:URL,origin:String){
  self.output=output;self.origin=origin
  let config=WKWebViewConfiguration();config.websiteDataStore = .nonPersistent()
  config.userContentController.addUserScript(WKUserScript(source:#"""
  window.__wr={calls:[],pending:false,wr:{configured:true,verified:true,wifi:true,busy:false,state:'idle',error:'',revision:3,books:[],progress:0,total:0,synced_books:0,synced_notes:0},meta:{request_id:0,revision:1,kind:0,total_bytes:0,busy:false,ready:false,error:''},raw:new Uint8Array()};
  const __kinds={search:1,info:2,chapters:3,progress:4,recommend:5,similar:6,stats:7,highlights:8,reviews:9};
  const __data={search:{hasMore:1,results:[{title:'电子书',scope:17,books:[{searchIdx:3,bookInfo:{bookId:'b1',title:'看不见的城市',author:'伊塔洛·卡尔维诺'},newRating:92},{searchIdx:8,bookInfo:{bookId:'b2',title:'宇宙的琴弦',author:'布莱恩·格林'},newRating:89},{searchIdx:12,bookInfo:{bookId:'b3',title:'<img src=x onerror=alert(1)>',author:'按纯文本显示'},newRating:88}]}]},info:{bookId:'b1',title:'看不见的城市',author:'伊塔洛·卡尔维诺',newRating:92,category:'文学 · 小说',publisher:'译林出版社',isbn:'9787544702123',wordCount:120000,intro:'城市不在于你能看见多少，而在于你能从中发现多少。\n\n这是一份离线测试图书资料，用于检查中文长段落、标点与窄屏排版。',readerLink:'https://weread.qq.com/web/reader/fixture'},chapters:{chapters:Array.from({length:200},(_,i)=>({title:'第 '+(i+1)+' 章 · 城市与记忆，城市与愿望',chapterUid:i+1,level:1,wordCount:1800,price:i<6?0:100,paid:i===6?1:0}))},progress:{book:{progress:1,recordReadingTime:3661,updateTime:1792828800}},recommend:{books:[{bookId:'b1',title:'看不见的城市',author:'伊塔洛·卡尔维诺',reason:'根据你的阅读记录推荐',newRating:92},{bookId:'b2',title:'宇宙的琴弦',author:'布莱恩·格林',reason:'发现另一个感兴趣的领域',newRating:89}]},similar:{booksimilar:{sessionId:'fixture-session',books:[{idx:1,book:{bookInfo:{bookId:'b4',title:'树上的男爵',author:'伊塔洛·卡尔维诺'}}}]}},stats:{totalReadTime:27900,dayAverageReadTime:1800,readDays:16,compare:0.2,readStat:[{stat:'读过',counts:'8本'},{stat:'读完',counts:'3本'},{stat:'笔记',counts:'27条'}],preferCategoryWord:'偏好阅读文学',preferTimeWord:'偏好夜间阅读',readLongest:[{book:{title:'看不见的城市'},readTime:12600},{book:{title:'宇宙的琴弦'},readTime:10800}],preferCategory:[{categoryTitle:'文学',readingTime:15000},{categoryTitle:'科学',readingTime:12900}]},highlights:{items:Array.from({length:20},(_,i)=>({markText:'你要理解的不在于一座城市的七种或七十种奇景，而在于它对你提出的问题所给予的回答。 '+(i+1),totalCount:200-i}))},reviews:{reviews:[{idx:5,review:{review:{author:{name:'阅读者'},star:100,createTime:1792828800,content:'城市与记忆互为镜像。'+('这是一条较长的公开书评，用于检查展开全文的交互。'.repeat(20))}}}],reviewsHasMore:1,synckey:33}};
  window.confirm=()=>true;
  window.fetch=async(path,opt={})=>{const f=window.__wr,b=opt.body?JSON.parse(opt.body):{};f.calls.push({path,method:opt.method,body:b});let status=200,body={};if(path.includes('/pico/weread/')){const action=path.split('/').at(-1);if(action==='query'){if(opt.method==='POST'){f.raw=new TextEncoder().encode(JSON.stringify(__data[b.kind]||{}));Object.assign(f.meta,{request_id:f.meta.request_id+1,kind:__kinds[b.kind],revision:f.meta.revision+1,total_bytes:f.raw.length,busy:f.pending,ready:!f.pending,error:''});status=202;}body=f.meta;}else if(action==='result'){let end=Math.min(f.raw.length,b.offset+b.limit);while(end<f.raw.length&&(f.raw[end]&0xc0)===0x80)end--;body={request_id:f.meta.request_id,offset:b.offset,next_offset:end,total_bytes:f.raw.length,chunk:new TextDecoder().decode(f.raw.slice(b.offset,end))};}else if(action==='cancel'){Object.assign(f.meta,{busy:false,ready:false,error:'cancelled'});}else body=f.wr;}else if(path.endsWith('/home'))body={config:{enabled:false,rooms:[]},saved_wifi:[],status:{result:0,revision:1},network_online:true};else if(path.endsWith('/printers'))body={printers:[],imports:[]};else if(path.endsWith('/bambu/auth'))body={session:0,revision:0,phase:0,region:0,count:0,result:0,error:0,http_status:0,seconds_left:0,retry_after:0,devices:[],config_revision:1,saved_count:0,saved_accounts:[]};return {ok:status>=200&&status<300,status,json:async()=>JSON.parse(JSON.stringify(body))};};
  """#,injectionTime:.atDocumentStart,forMainFrameOnly:true))
  view=WKWebView(frame:NSRect(x:0,y:0,width:390,height:844),configuration:config)
  window=NSWindow(contentRect:view.frame,styleMask:.borderless,backing:.buffered,defer:false)
  super.init();view.navigationDelegate=self;window.contentView=view;window.setFrameOrigin(NSPoint(x:-12000,y:-12000));window.orderFront(nil)
 }
 func webView(_ webView:WKWebView,didFinish navigation:WKNavigation!){pending?.resume();pending=nil}
 func webView(_ webView:WKWebView,didFail navigation:WKNavigation!,withError error:Error){pending?.resume(throwing:error);pending=nil}
 func webView(_ webView:WKWebView,didFailProvisionalNavigation navigation:WKNavigation!,withError error:Error){pending?.resume(throwing:error);pending=nil}
 func js(_ code:String) async throws -> Any? {try await view.callAsyncJavaScript(code,arguments:[:],in:nil,contentWorld:.page)}
 func check(_ code:String,_ name:String) async throws {checks+=1;guard try await js("return (\(code));") as? Bool == true else{throw TestError.failed(name)};print("PASS \(name)")}
 func load() async throws {try await withCheckedThrowingContinuation{c in pending=c;view.load(URLRequest(url:URL(string:origin+"/manage.html?fixture=\(UUID().uuidString)#key=0123456789abcdef0123456789abcdef&view=weread")!))};_ = try await js("await loadAll();await loadWeRead();showWeReadPane('online');document.body.getBoundingClientRect();return true;")}
 func snap(_ name:String) async throws {let image:NSImage=try await withCheckedThrowingContinuation{c in view.takeSnapshot(with:nil){i,e in if let i{c.resume(returning:i)}else{c.resume(throwing:e ?? TestError.failed("snapshot"))}}};guard let t=image.tiffRepresentation,let b=NSBitmapImageRep(data:t),let png=b.representation(using:.png,properties:[:])else{throw TestError.failed("png")};try png.write(to:output.appendingPathComponent(name+".png"));screenshots+=1}
 func frameContent() async throws {_ = try await js("document.getElementById('wrOnlineContent').scrollIntoView({block:'start'});await new Promise(r=>setTimeout(r,40));return true;")}
 func run() async throws {
  for width in [320,390,768] {for dark in [false,true] {
   let label="\(width)-\(dark ? "dark":"light")"
   window.setContentSize(NSSize(width:width,height:width==768 ? 1024:844));window.appearance=NSAppearance(named:dark ? .darkAqua:.aqua);view.appearance=window.appearance
   try await load();try await check("state.tab==='weread'&&!document.getElementById('wrOnlinePane').hidden&&document.getElementById('wrSyncPane').hidden","real pane navigation \(label)")
   _ = try await js("await queryWeRead('search',{keyword:'城市',scope:10,count:12,cursor:0});return true;")
   try await check("document.getElementById('wrOnlineContent').textContent.includes('看不见的城市')&&!document.querySelector('#wrOnlineContent img')","literal search content \(label)")
   try await check("document.documentElement.scrollWidth<=innerWidth","search no horizontal overflow \(label)")
   try await frameContent();try await snap(label+"-search")
   _ = try await js("await queryWeRead('info',{book_id:'b1'});return true;")
   try await check("[...document.querySelectorAll('#wrOnlineContent button')].map(e=>e.textContent).join('|')==='章节目录|阅读进度|热门划线|公开书评|相似书籍'","all detail actions \(label)")
   try await check("[...document.querySelectorAll('#wrOnlineContent .wrshortcuts button')].every(e=>e.getBoundingClientRect().height<=60)","detail action labels stay horizontal \(label)")
   try await check("document.querySelector('#wrOnlineContent a').getAttribute('rel')==='noopener noreferrer'&&document.querySelector('#wrOnlineContent a').target==='_blank'","official reader link \(label)")
   try await frameContent();try await snap(label+"-detail")
   _ = try await js("await queryWeRead('chapters',{book_id:'b1'});return true;")
   try await check("weReadOnlineState().items.length===200&&document.querySelectorAll('#wrOnlineContent .wrentry').length===12&&!document.getElementById('wrOnlinePager').hidden","full chunked directory locally paginated \(label)")
   try await frameContent();try await snap(label+"-chapters")
   _ = try await js("await queryWeRead('stats',{mode:1,base_time:0});return true;")
   try await check("document.getElementById('wrOnlineContent').textContent.includes('7 小时 45 分钟')&&document.documentElement.scrollWidth<=innerWidth","stats seconds and layout \(label)")
   try await check("[...document.querySelectorAll('#wrOnlinePane button,#wrOnlinePane input,#wrOnlinePane select')].filter(e=>e.getBoundingClientRect().width).every(e=>{const r=e.getBoundingClientRect();return r.height>=44&&r.left>=0&&r.right<=innerWidth})","44pt controls fit \(label)")
   try await frameContent();try await snap(label+"-stats")
  }}
  try await load();_ = try await js("await queryWeRead('progress',{book_id:'b1'});return true;")
  try await check("document.getElementById('wrOnlineContent').textContent.includes('1%')&&!document.getElementById('wrOnlineContent').textContent.includes('100%')","actual progress uses percent not ratio")
  _ = try await js("await queryWeRead('reviews',{book_id:'b1',count:12,cursor:0,synckey:0});return true;")
  try await check("document.querySelector('#wrOnlineContent details')&&!document.querySelector('#wrOnlineContent details').open&&document.getElementById('wrOnlineContent').textContent.includes('5 / 5 星')","real expandable double-nested review")
  _ = try await js("document.querySelector('#wrOnlineContent summary').click();return true;")
  try await check("document.querySelector('#wrOnlineContent details').open","review expands with native details")
  _ = try await js("window.__wr.pending=true;await queryWeRead('recommend',{count:12,cursor:0});return true;")
  try await check("weReadOnlineState().busy&&!document.getElementById('wrOnlineCancel').hidden&&document.getElementById('wrSearchButton').disabled","real pending task blocks only duplicate actions")
  _ = try await js("window.__wr.meta.request_id++;window.__wr.meta.busy=false;window.__wr.meta.ready=true;await pollWeReadQuery();return true;")
  try await check("document.getElementById('wrOnlineStatus').textContent.includes('另一项设备操作替换')","actual WK rejects superseded result")
  _ = try await js("window.__wr.pending=true;await queryWeRead('info',{book_id:'b1'});window.__wr.meta.ready=true;window.__wr.wr.busy=true;window.__wr.wr.state='query';await loadWeRead();await pollWeReadQuery();return true;")
  try await check("window.__wr.meta.busy&&!weReadOnlineState().busy&&document.getElementById('wrOnlineContent').textContent.includes('看不见的城市')","ready INFO text shows before optional cover worker finishes")
  _ = try await js("window.__wr.pending=false;window.__wr.meta.busy=false;window.__wr.wr.busy=false;window.__wr.wr.state='done';window.__wr.wr.revision++;await loadWeRead();return true;")
  _ = try await js("document.documentElement.style.fontSize='200%';document.body.style.fontSize='2rem';await queryWeRead('info',{book_id:'b1'});return true;")
  try await check("document.documentElement.scrollWidth<=innerWidth","200 percent text keeps document inside screen")
  _ = try await js("forgetKey();return true;")
  try await check("state.weread===null&&!document.getElementById('wrOnlineContent').textContent.includes('看不见的城市')","pairing termination clears real online DOM")
  let data=try JSONSerialization.data(withJSONObject:["checks":checks,"screenshots":screenshots,"status":"PASS","production_html":"components/read_pico_transfer/manage.html","network":"injected fixtures only; no real account","hardware_access":false],options:[.prettyPrinted,.sortedKeys]);try data.write(to:output.appendingPathComponent("report.json"));print("PASS WKWebView \(checks) checks, \(screenshots) screenshots")
 }
}
@main struct Main {static func main(){let app=NSApplication.shared;app.setActivationPolicy(.prohibited);Task{@MainActor in do{let out=URL(fileURLWithPath:CommandLine.arguments[1]);try FileManager.default.createDirectory(at:out,withIntermediateDirectories:true);try await WeReadWebTest(output:out,origin:CommandLine.arguments[2]).run();exit(0)}catch{fputs("FAIL \(error)\n",stderr);exit(1)}};app.run()}}
