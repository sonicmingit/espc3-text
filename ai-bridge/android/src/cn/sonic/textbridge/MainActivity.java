package cn.sonic.textbridge;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.*;
import android.webkit.*;
import android.widget.FrameLayout;
import org.json.*;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class MainActivity extends Activity {
    static final UUID SERVICE=UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    static final UUID RX=UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    static final UUID TX=UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
    static final UUID CCCD=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    final Handler handler=new Handler(Looper.getMainLooper());
    final Map<String,BluetoothDevice> found=new LinkedHashMap<>();
    final ArrayDeque<byte[]> writes=new ArrayDeque<>();
    final ByteArrayOutputStream received=new ByteArrayOutputStream();
    BluetoothAdapter adapter;BluetoothLeScanner scanner;BluetoothGatt gatt;
    BluetoothGattCharacteristic rx,tx;BluetoothDevice bonding;
    WebView web;boolean ready=false,writing=false,discovering=false,scanning=false,loaded=false;
    int chunkSize=20,writeGeneration=0,connectGeneration=0;String sharedText;

    void emit(JSONObject value){handler.post(()->{if(web!=null&&loaded)web.evaluateJavascript("window.onNative("+JSONObject.quote(value.toString())+")",null);});}
    void event(String kind,String message){JSONObject j=new JSONObject();try{j.put("kind",kind);j.put("message",message);j.put("ready",ready);}catch(Exception ignored){}emit(j);}
    void status(String message){event("link",message);}
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        adapter=((BluetoothManager)getSystemService(BLUETOOTH_SERVICE)).getAdapter();
        FrameLayout root=new FrameLayout(this);root.setBackgroundColor(0xfff8fafc);
        root.setOnApplyWindowInsetsListener((v,insets)->{v.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());return insets.consumeSystemWindowInsets();});
        web=new WebView(this);WebSettings settings=web.getSettings();settings.setJavaScriptEnabled(true);settings.setDomStorageEnabled(false);settings.setAllowFileAccess(false);settings.setAllowContentAccess(false);settings.setAllowFileAccessFromFileURLs(false);settings.setAllowUniversalAccessFromFileURLs(false);settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        web.addJavascriptInterface(new Bridge(),"Android");
        web.setWebViewClient(new WebViewClient(){
            @Override public boolean shouldOverrideUrlLoading(WebView v,WebResourceRequest r){return true;}
            @Override public void onPageFinished(WebView v,String url){loaded=true;if(sharedText!=null){v.evaluateJavascript("window.onSharedText("+JSONObject.quote(sharedText)+")",null);sharedText=null;}}
        });
        root.addView(web,new FrameLayout.LayoutParams(-1,-1));setContentView(root);
        if(Intent.ACTION_SEND.equals(getIntent().getAction()))sharedText=getIntent().getStringExtra(Intent.EXTRA_TEXT);
        web.loadUrl("file:///android_asset/index.html");
        IntentFilter filter=new IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
        if(Build.VERSION.SDK_INT>=33)registerReceiver(bondReceiver,filter,Context.RECEIVER_EXPORTED);else registerReceiver(bondReceiver,filter);
    }
    @Override protected void onNewIntent(Intent intent){super.onNewIntent(intent);setIntent(intent);if(Intent.ACTION_SEND.equals(intent.getAction())){String text=intent.getStringExtra(Intent.EXTRA_TEXT);if(text!=null&&loaded)web.evaluateJavascript("window.onSharedText("+JSONObject.quote(text)+")",null);}}
    boolean permissions(){
        if(Build.VERSION.SDK_INT>=31)return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)==PackageManager.PERMISSION_GRANTED&&checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED;
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)==PackageManager.PERMISSION_GRANTED;
    }
    void scan(){
        if(adapter==null){event("error","这台设备不支持蓝牙。");return;}
        if(!permissions()){
            if(Build.VERSION.SDK_INT>=31)requestPermissions(new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT},10);
            else requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION},10);return;
        }
        if(!adapter.isEnabled()){status("请开启蓝牙后重新搜索。");startActivity(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));return;}
        if(gatt!=null){status("请先断开当前设备再搜索。");return;}
        stopScan();found.clear();event("scanStart","正在搜索 Sonic-AI…");scanner=adapter.getBluetoothLeScanner();
        if(scanner==null){event("error","无法启动蓝牙扫描，请重新开启蓝牙。");return;}
        scanning=true;
        try{scanner.startScan(Collections.singletonList(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICE)).build()),new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),scanCallback);handler.postDelayed(()->{if(scanning){stopScan();event("scanEnd",found.isEmpty()?"未找到板子，请确认已刷入 AI 版固件并靠近手机。安卓 11 及更早版本还需开启系统定位。":"搜索结束，点击设备连接。");}},15000);}catch(SecurityException e){event("error","请在系统设置中允许附近设备权限。");}
    }
    void stopScan(){if(scanner!=null&&scanning){try{scanner.stopScan(scanCallback);}catch(Exception ignored){}}scanning=false;}
    final ScanCallback scanCallback=new ScanCallback(){
        @Override public void onScanResult(int type,ScanResult result){handler.post(()->{
            BluetoothDevice device=result.getDevice();String address=device.getAddress();if(found.containsKey(address))return;found.put(address,device);
            JSONObject j=new JSONObject();try{j.put("kind","device");j.put("name",device.getName()==null?"Sonic-AI":device.getName());j.put("address",address);j.put("rssi",result.getRssi());}catch(Exception ignored){}emit(j);
        });}
        @Override public void onScanFailed(int code){handler.post(()->{scanning=false;event("error","蓝牙搜索失败，请稍后重试，错误码 "+code);});}
    };
    void connect(String address){
        if(!permissions()){event("error","请先允许蓝牙权限并搜索设备。");return;}
        BluetoothDevice device=found.get(address);if(device==null){event("error","请重新搜索并选择设备。");return;}
        stopScan();disconnect(false);status("正在连接，请留意系统蓝牙配对提示…");
        connectGatt(device);
    }
    final BroadcastReceiver bondReceiver=new BroadcastReceiver(){
        @Override public void onReceive(Context context,Intent intent){
            BluetoothDevice device=intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);if(bonding==null||device==null||!bonding.getAddress().equals(device.getAddress()))return;
            int state=intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE,BluetoothDevice.BOND_NONE);
            if(state==BluetoothDevice.BOND_BONDED){bonding=null;if(gatt!=null)prepareGatt(gatt);else connectGatt(device);}else if(state==BluetoothDevice.BOND_NONE){bonding=null;disconnect(false);event("error","配对未完成，请重试；若板子已清除配对记录，请在手机系统里忘记此设备后再连接。");}
        }
    };
    void connectGatt(BluetoothDevice device){
        ready=false;discovering=false;chunkSize=20;int generation=++connectGeneration;
        try{gatt=device.connectGatt(this,false,gattCallback,BluetoothDevice.TRANSPORT_LE);}catch(Exception e){event("error","蓝牙连接失败，请重试。");return;}
        handler.postDelayed(()->{if(generation==connectGeneration&&!ready&&gatt!=null){disconnect(false);event("error","连接超时，请靠近板子后重试。");}},25000);
    }
    void discover(BluetoothGatt current){if(current!=gatt||discovering)return;discovering=true;if(!current.discoverServices()){disconnect(false);event("error","无法读取设备服务，请重新连接。");}}
    void prepareGatt(BluetoothGatt current){status("正在读取设备服务…");boolean requested=current.requestMtu(247);if(!requested)discover(current);else handler.postDelayed(()->discover(current),2000);}
    final BluetoothGattCallback gattCallback=new BluetoothGattCallback(){
        @Override public void onConnectionStateChange(BluetoothGatt current,int error,int state){handler.post(()->{
            if(current!=gatt)return;
            if(error!=BluetoothGatt.GATT_SUCCESS||state==BluetoothProfile.STATE_DISCONNECTED){disconnect(false);status("蓝牙已断开，草稿和历史仍保留在手机中。");return;}
            if(state==BluetoothProfile.STATE_CONNECTED){
                BluetoothDevice device=current.getDevice();
                if(device.getBondState()!=BluetoothDevice.BOND_BONDED){bonding=device;status("正在配对，请确认系统蓝牙提示…");try{if(!device.createBond()){disconnect(false);event("error","无法开始配对，请重试。");}}catch(Exception e){disconnect(false);event("error","蓝牙配对失败，请检查权限。");}}
                else prepareGatt(current);
            }
        });}
        @Override public void onMtuChanged(BluetoothGatt current,int mtu,int error){handler.post(()->{if(current!=gatt)return;if(error==BluetoothGatt.GATT_SUCCESS)chunkSize=Math.max(20,Math.min(180,mtu-3));discover(current);});}
        @Override public void onServicesDiscovered(BluetoothGatt current,int error){handler.post(()->{
            if(current!=gatt)return;BluetoothGattService service=current.getService(SERVICE);
            if(error!=0||service==null){disconnect(false);event("error","未找到文字桥服务，请确认固件版本。");return;}
            rx=service.getCharacteristic(RX);tx=service.getCharacteristic(TX);
            if(rx==null||tx==null||!current.setCharacteristicNotification(tx,true)){disconnect(false);event("error","设备服务不完整，请重新连接。");return;}
            BluetoothGattDescriptor descriptor=tx.getDescriptor(CCCD);if(descriptor==null){disconnect(false);event("error","设备缺少通知服务。");return;}
            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);if(!current.writeDescriptor(descriptor)){disconnect(false);event("error","无法订阅设备状态，请重试。");}
        });}
        @Override public void onDescriptorWrite(BluetoothGatt current,BluetoothGattDescriptor descriptor,int error){handler.post(()->{
            if(current!=gatt)return;if(error!=0){disconnect(false);event("error","订阅状态失败，请重新配对连接。");return;}
            ready=true;status("蓝牙已连接，可以发送文字。");sendWire("{\"op\":\"status\"}");pollStatus(connectGeneration);
        });}
        @Override public void onCharacteristicWrite(BluetoothGatt current,BluetoothGattCharacteristic characteristic,int error){handler.post(()->{
            if(current!=gatt)return;++writeGeneration;
            if(error!=0){disconnect(false);event("error","蓝牙发送失败，请重新连接后检查接收情况。");return;}
            if(!writes.isEmpty())writes.removeFirst();writing=false;writeNext();
        });}
        @Override public void onCharacteristicChanged(BluetoothGatt current,BluetoothGattCharacteristic characteristic){byte[] value=characteristic.getValue();if(value!=null)receive(current,value);}
        @Override public void onCharacteristicChanged(BluetoothGatt current,BluetoothGattCharacteristic characteristic,byte[] value){receive(current,value);}
    };
    void receive(BluetoothGatt current,byte[] value){handler.post(()->{
        if(current!=gatt)return;
        for(byte b:value){if(b==10){String text=new String(received.toByteArray(),StandardCharsets.UTF_8);received.reset();try{emit(new JSONObject(text));}catch(Exception ignored){}}else if(received.size()<65536)received.write(b);else{received.reset();event("error","接收数据过长，请重新连接。");}}
    });}
    void pollStatus(int generation){if(!ready||generation!=connectGeneration)return;sendWire("{\"op\":\"status\"}");handler.postDelayed(()->pollStatus(generation),5000);}
    void sendWire(String json){
        if(!ready||gatt==null||rx==null){event("error","请先连接板子。");return;}
        byte[] data=(json+"\n").getBytes(StandardCharsets.UTF_8);
        if(data.length>12288){event("error","发送内容过长，请分成几段。");return;}
        if(writes.size()>200){event("error","正在发送，请稍后再试。");return;}
        for(int i=0;i<data.length;i+=chunkSize)writes.addLast(Arrays.copyOfRange(data,i,Math.min(i+chunkSize,data.length)));writeNext();
    }
    void writeNext(){
        if(writing||writes.isEmpty()||!ready||gatt==null)return;
        writing=true;rx.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);rx.setValue(writes.peekFirst());int generation=++writeGeneration;
        try{if(!gatt.writeCharacteristic(rx)){disconnect(false);event("error","蓝牙写入失败，请重新连接。");return;}}catch(Exception e){disconnect(false);event("error","蓝牙发送失败，请检查权限。");return;}
        handler.postDelayed(()->{if(writing&&generation==writeGeneration){disconnect(false);event("error","蓝牙发送超时，请重新连接后再试。");}},12000);
    }
    void disconnect(boolean inform){
        ++connectGeneration;++writeGeneration;bonding=null;ready=false;writing=false;discovering=false;writes.clear();received.reset();rx=null;tx=null;
        BluetoothGatt old=gatt;gatt=null;if(old!=null){try{old.disconnect();old.close();}catch(Exception ignored){}}
        if(inform)status("已断开连接。");
    }
    class Bridge {
        @JavascriptInterface public String load(){return getSharedPreferences("state",MODE_PRIVATE).getString("json","{}");}
        @JavascriptInterface public void save(String json){if(json.length()<1600000)getSharedPreferences("state",MODE_PRIVATE).edit().putString("json",json).apply();}
        @JavascriptInterface public void scan(){handler.post(()->MainActivity.this.scan());}
        @JavascriptInterface public void connect(String address){handler.post(()->MainActivity.this.connect(address));}
        @JavascriptInterface public void disconnect(){handler.post(()->MainActivity.this.disconnect(true));}
        @JavascriptInterface public void send(String json){handler.post(()->sendWire(json));}
        @JavascriptInterface public void copy(String text){handler.post(()->{android.content.ClipboardManager c=(android.content.ClipboardManager)getSystemService(CLIPBOARD_SERVICE);c.setPrimaryClip(ClipData.newPlainText("文字",text));event("copied","已复制到剪贴板。");});}
        @JavascriptInterface public void openConfig(String ip){handler.post(()->{if(!ip.matches("(?:[0-9]{1,3}\\.){3}[0-9]{1,3}"))return;try{startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse("http://"+ip+"/")));}catch(Exception e){event("error","无法打开浏览器。");}});}
    }
    @Override public void onRequestPermissionsResult(int request,String[] names,int[] granted){super.onRequestPermissionsResult(request,names,granted);if(request==10){if(permissions())scan();else event("error","未获得蓝牙权限，请在系统设置中允许附近设备。");}}
    @Override protected void onPause(){if(web!=null&&loaded)web.evaluateJavascript("window.persist&&window.persist()",null);stopScan();super.onPause();}
    @Override protected void onDestroy(){stopScan();disconnect(false);handler.removeCallbacksAndMessages(null);try{unregisterReceiver(bondReceiver);}catch(Exception ignored){}if(web!=null){web.removeJavascriptInterface("Android");web.destroy();web=null;}super.onDestroy();}
}
