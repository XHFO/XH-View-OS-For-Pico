/* SPDX-License-Identifier: Apache-2.0
 * 调用实际产品阅读控制器输出同源原生画布。 / Render the actual reader controller to a native host canvas.
 */
#define PICO_READER_REAL_RENDER
#define main reader_host_test_main
#include "pico_reader_host_test.c"
#undef main
#include "pico_framebuffer_host.h"
static void save_scene(const char *dir,const char *name){
    char path[512];snprintf(path,sizeof(path),"%s/%s-%s.pgm",dir,name,view.landscape?"landscape":"portrait");
    uint8_t *fb=pico_fb_new();assert(fb);ps_reader_render(fb);assert(pico_fb_save(path,fb));free(fb);printf("%s\n",path);
}
int main(int argc,char **argv){
    assert(argc==3);assert(pico_fb_init(argv[1]));snprintf(rootdir,sizeof(rootdir),"./library");assert(!mkdir(rootdir,0700));assert(!mkdir("./library/books",0700));ps_reader_init();assert(rr&&pages);
    const char *local_names[]={"沿河的书店.txt","已读书籍.txt","日常的尺度.epub","远行手记.txt","山间来信.epub","字体与标点试读页.txt","一本书名很长很长需要自动省略显示的本机图书.txt"};
    for(int i=0;i<7;i++)file_fixture(local_names[i],i%2==0);
    ps_navigate(PS_SHELF);scan_complete();
    for(int land=0;land<2;land++){
        view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        ps_navigate(PS_SHELF);scan_complete();shelf_manage=false;shelf_scroll=0;shelf_filter=0;rp.list=1;
        shelf_ready=false;shelf_commit_pending=true;save_scene(argv[2],"library-loading");shelf_commit_pending=false;shelf_ready=true;save_scene(argv[2],"library-local");
        save_scene(argv[2],"library-refresh-before");reader_shelf_layout_t refresh=shelf_layout();
        ps_event_t refresh_event={.type=PS_TAP,.x=refresh.refresh_x+20,.y=refresh.tabs_y+15};
        assert(!ps_reader_event(&refresh_event));now_ms+=20;assert(!ps_reader_tick());save_scene(argv[2],"library-refresh-held");scan_complete();save_scene(argv[2],"library-refresh-after");
        shelf_scroll=1;save_scene(argv[2],"library-local-page2");shelf_scroll=0;rp.list=1;save_scene(argv[2],"library-local-list");shelf_manage=true;save_scene(argv[2],"library-local-management");shelf_manage=false;rp.list=0;save_scene(argv[2],"library-local-covers");
        rp.list=SHELF_VIEW_GALLERY;save_scene(argv[2],"library-local-gallery");rp.list=SHELF_VIEW_FOCUS;save_scene(argv[2],"library-local-focus");
        rp.list=SHELF_VIEW_COMPACT;save_scene(argv[2],"library-local-compact");shelf_style_picker=true;save_scene(argv[2],"library-local-styles");shelf_style_picker=false;rp.list=SHELF_VIEW_COVERS;
        search_done("一本书名");save_scene(argv[2],"library-local-long-name");search_done("");
        ps_reader_open(3);complete();save_scene(argv[2],"reading");
        tools=true;save_scene(argv[2],"reading-tools");chapter_panel=true;save_scene(argv[2],"reading-chapters");tools=chapter_panel=false;
        const char *faces[]={"builtin","builtin-serif","builtin-kai"},*names[]={"reading-sans","reading-serif","reading-kai"};
        anchor=chapters[2].offset;remember();size_t kept=anchor;
        for(int i=0;i<3;i++){
            ri_copy(rp.font,sizeof(rp.font),faces[i]);reader_font_sync(true);layout_begin();complete();
            assert(!strcmp(ttf_font_path(),faces[i])&&anchor==kept&&pages[current_page]<=kept&&page_end()>kept);
            save_scene(argv[2],names[i]);
        }
        ri_copy(rp.font,sizeof(rp.font),"builtin");reader_font_sync(true);layout_begin();complete();
        int saved_size=rp.size;
        for(int size=15;size<=17;size+=2){
            char name[40];snprintf(name,sizeof(name),"reading-legacy%d",size);
            rp.size=(uint8_t)size;layout_begin();complete();assert(anchor==kept);save_scene(argv[2],name);
        }
        rp.size=(uint8_t)saved_size;layout_begin();complete();
        current_page=page_count/3;anchor=pages[current_page];selection_start=anchor;
        while(selection_start<rb[3].bytes&&strchr(" \t\r\n",rb[3].text[selection_start]))selection_start++;
        selection_end=selection_start+84;while(((unsigned char)rb[3].text[selection_end]&0xc0)==0x80)selection_end++;selection_active=true;
        // 保存划线会结束选区；笔记截图重新选中同一原文，避免展示成无引用的当前位置笔记。
        // Saving a highlight ends selection; reselect the same text for the note fixture to retain its quotation.
        if(!rr->count){save_highlight();selection_active=true;new_note();editor_append("在缓慢的日常里，重新发现值得停留的风景。这里保存了真实阅读位置，改变字号后仍能返回原文。");editor_commit();}
        ps_navigate(PS_RECORDS);record_tab=REC_NOTE;selected_record=-1;save_scene(argv[2],"records");
        selected_record=(int)rr->count-1;assert(selected_record>=0&&rr->item[selected_record]->type==REC_NOTE&&rr->item[selected_record]->quote[0]);
        reader_record_t *original=rr->item[selected_record],display=*original;char detail[PS_READER_NOTE_MAX_BYTES+1];
        u8copy(detail,sizeof(detail),rb[3].text+chapters[2].offset,1200);display.text=detail;rr->item[selected_record]=&display;save_scene(argv[2],"record-detail");
        display.ink_count=3;display.ink[0]=(reader_ink_t){0x8000|100,200};display.ink[1]=(reader_ink_t){400,800};display.ink[2]=(reader_ink_t){900,300};save_scene(argv[2],"record-ink-detail");rr->item[selected_record]=original;selected_record=-1;
        ps_edit("写笔记","阅读让日常多一些值得停留的风景。",383,NULL);editor_mode(INPUT_PINYIN);strcpy(composition,"yuedu");candidates_update();save_scene(argv[2],"keyboard26");
        strcpy(composition,"woxihuanyuedu");candidates_update();save_scene(argv[2],"keyboard-sentence");
        strcpy(composition,"yued");candidates_update();save_scene(argv[2],"keyboard-completion");
        strcpy(composition,"shi");candidates_update();candidate_page=candidate_page_end(0,(view.landscape?404:344)-80);save_scene(argv[2],"keyboard-candidates-page2");
        editor_mode(INPUT_T9);strcpy(composition,"98338");candidates_update();save_scene(argv[2],"keyboard9");
        editor_mode(INPUT_ENGLISH);save_scene(argv[2],"keyboard-english");editor_shift=true;save_scene(argv[2],"keyboard-uppercase");editor_mode(INPUT_SYMBOL);save_scene(argv[2],"keyboard-symbols");
        ps_edit_password("Wi-Fi 密码",64,NULL);editor_append("ReadPico!@2026");save_scene(argv[2],"wifi-password");
        editor_password_visible=true;save_scene(argv[2],"wifi-password-visible");editor_password_visible=false;
        editor_mode(INPUT_SYMBOL);save_scene(argv[2],"wifi-password-numbers");editor_symbol_page=1;save_scene(argv[2],"wifi-password-symbols");
        ps_reader_open(4);save_scene(argv[2],"comic");
        ps_navigate(PS_READING_SETTINGS);save_scene(argv[2],"reading-settings");
        ps_navigate(PS_INPUT_SETTINGS);save_scene(argv[2],"input-settings");
        ps_navigate(PS_IMPORT);save_scene(argv[2],"import");
    }
    shelf_scan_close();shelf_cover_display_release();free(shelf_stage_files);free(shelf_files);records_free(rr);free(pages);blobs_clear(blobs);blobs_clear(source_blobs);pico_fb_close();return 0;
}
