# nagios4-cn  
Nagios4.x监控项目中文版  
版本 v1.0  
大部分翻译取自nagios-cn( http://sourceforge.net/projects/nagios-cn/ )项目，修正了部分汉化  
汉化工作已经基本完成，cgi/*json.c部分文件未汉化，nagios数据json格式化与基本使用无碍  
如发现任何问题或修改建议请联系 imniuba(AT)gmail.com  
扩展补丁列表:  
>nagios-cgi-http_charset.patch, 在cgi.cfg中添加http_charset参数，用于指定http的字符编码格式，默认utf8  
>nagios-googlemap.patch, 在statusmap.c中添加google地图扩展  
>nagios-disabled-query_update.patch, 关闭了自动检测升级  
>nagios-archivelog-timeformat.patch, 修正归档日志文件命名格式"nagios-%04d-%02d-%02d-%02d.log"  
>nagios-output-length.patch, 提高了扩展命令、插件的最大输出字符长度  
  
