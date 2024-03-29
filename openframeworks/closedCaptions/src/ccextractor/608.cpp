#include "ccextractor.h"

int     rowdata[] = {11,-1,1,2,3,4,12,13,14,15,5,6,7,8,9,10};
// Relationship between the first PAC byte and the row number

int new_channel = 1; // The new channel after a channel change
int in_xds_mode = 0;
extern int ts_headers_total;

#define INITIAL_ENC_BUFFER_CAPACITY		2048

unsigned char *enc_buffer=NULL; // Generic general purpose buffer
unsigned char str[2048]; // Another generic general purpose buffer
unsigned enc_buffer_used;
unsigned enc_buffer_capacity;


int general_608_init (void)
{
    enc_buffer=(unsigned char *) malloc (INITIAL_ENC_BUFFER_CAPACITY);
    if (enc_buffer==NULL)
        return -1;
    enc_buffer_capacity=INITIAL_ENC_BUFFER_CAPACITY;
    return 0;
}

// Preencoded strings
unsigned char encoded_crlf[16]; 
unsigned int encoded_crlf_length;
unsigned char encoded_br[16];
unsigned int encoded_br_length;

unsigned char *subline; // Temp storage for .srt lines
int new_sentence=1; // Capitalize next letter?

// Default color
unsigned char usercolor_rgb[8]="";
color_code default_color=COL_WHITE;

const char *sami_header= // TODO: Revise the <!-- comments
"<SAMI>\n\
<HEAD>\n\
<STYLE TYPE=\"text/css\">\n\
<!--\n\
P {margin-left: 16pt; margin-right: 16pt; margin-bottom: 16pt; margin-top: 16pt;\n\
text-align: center; font-size: 18pt; font-family: arial; font-weight: bold; color: #f0f0f0;}\n\
.UNKNOWNCC {Name:Unknown; lang:en-US; SAMIType:CC;}\n\
-->\n\
</STYLE>\n\
</HEAD>\n\n\
<BODY>\n";

const char *command_type[] =
{
    "Unknown",
    "EDM - EraseDisplayedMemory",
    "RCL - ResumeCaptionLoading",
    "EOC - End Of Caption",
    "TO1 - Tab Offset, 1 column",
    "TO2 - Tab Offset, 2 column",
    "TO3 - Tab Offset, 3 column",
    "RU2 - Roll up 2 rows",
    "RU3 - Roll up 3 rows",
    "RU4 - Roll up 4 rows",
    "CR  - Carriage Return",
    "ENM - Erase non-displayed memory",
    "BS  - Backspace",
    "RTD - Resume Text Display"
};

const char *font_text[]=
{
    "regular",
    "italics",
    "underlined",
    "underlined italics"
};

const char *cc_modes_text[]=
{
    "Pop-Up captions"
};

const char *color_text[][2]=
{
    {"white",""},
    {"green","<font color=\"#00ff00\">"},
    {"blue","<font color=\"#0000ff\">"},
    {"cyan","<font color=\"#00ffff\">"},
    {"red","<font color=\"#ff0000\">"},
    {"yellow","<font color=\"#ffff00\">"},
    {"magenta","<font color=\"#ff00ff\">"},
    {"userdefined","<font color=\""}
};

void clear_eia608_cc_buffer (struct eia608_screen *data)
{
    for (int i=0;i<15;i++)
    {
        memset(data->characters[i],' ',CC608_SCREEN_WIDTH);
        data->characters[i][CC608_SCREEN_WIDTH]=0;		
        memset (data->colors[i],default_color,CC608_SCREEN_WIDTH+1); 
        memset (data->fonts[i],FONT_REGULAR,CC608_SCREEN_WIDTH+1); 
        data->row_used[i]=0;        
    }
    data->empty=1;
}

void init_eia608 (struct eia608 *data)
{
    data->cursor_column=0;
    data->cursor_row=0;
    clear_eia608_cc_buffer (&data->buffer1);
    clear_eia608_cc_buffer (&data->buffer2);
    data->visible_buffer=1;
    data->last_c1=0;
    data->last_c2=0;
    data->mode=MODE_POPUP;
    // data->current_visible_start_cc=0;
    data->current_visible_start_ms=0;
    data->srt_counter=0;
    data->screenfuls_counter=0;
    data->channel=1;	
    data->color=default_color;
    data->font=FONT_REGULAR;
    data->rollup_base_row=14;
}

eia608_screen *get_writing_buffer (struct s_write *wb)
{
    eia608_screen *use_buffer=NULL;
    switch (wb->data608->mode)
    {
        case MODE_POPUP: // Write on the non-visible buffer
            if (wb->data608->visible_buffer==1)
                use_buffer = &wb->data608->buffer2;
            else
                use_buffer = &wb->data608->buffer1;
            break;
        case MODE_ROLLUP_2: // Write directly to screen
        case MODE_ROLLUP_3:
        case MODE_ROLLUP_4:
            if (wb->data608->visible_buffer==1)
                use_buffer = &wb->data608->buffer1;
            else
                use_buffer = &wb->data608->buffer2;
            break;
        default:
            fatal (EXIT_BUG_BUG, "Caption mode has an illegal value at get_writing_buffer(), this is a bug.\n");
    }
    return use_buffer;
}

void write_char (const unsigned char c, struct s_write *wb)
{
    if (wb->data608->mode!=MODE_TEXT)
    {
        eia608_screen * use_buffer=get_writing_buffer(wb);
        /* printf ("\rWriting char [%c] at %s:%d:%d\n",c,
        use_buffer == &wb->data608->buffer1?"B1":"B2",
        wb->data608->cursor_row,wb->data608->cursor_column); */
        use_buffer->characters[wb->data608->cursor_row][wb->data608->cursor_column]=c;
        use_buffer->colors[wb->data608->cursor_row][wb->data608->cursor_column]=wb->data608->color;
        use_buffer->fonts[wb->data608->cursor_row][wb->data608->cursor_column]=wb->data608->font;	
        use_buffer->row_used[wb->data608->cursor_row]=1;
        use_buffer->empty=0;
        if (wb->data608->cursor_column<31)
            wb->data608->cursor_column++;
    }

}

/* Handle MID-ROW CODES. */
void handle_text_attr (const unsigned char c1, const unsigned char c2, struct s_write *wb)
{
    // Handle channel change
    wb->data608->channel=new_channel;
    if (wb->data608->channel!=cc_channel)
        return;
    if (debug_608)
        printf ("\r608: text_attr: %02X %02X",c1,c2);
    if ( ((c1!=0x11 && c1!=0x19) ||
        (c2<0x20 || c2>0x2f)) && debug_608)
    {
        printf ("\rThis is not a text attribute!\n");
    }
    else
    {
        int i = c2-0x20;
        wb->data608->color=pac2_attribs[i][0];
        wb->data608->font=pac2_attribs[i][1];
        if (debug_608)
            printf ("  --  Color: %s,  font: %s\n",
            color_text[wb->data608->color][0],
            font_text[wb->data608->font]);
        if (wb->data608->cursor_column<31)
            wb->data608->cursor_column++;
    }
}


void write_subtitle_file_footer (struct s_write *wb)
{
    switch (write_format)
    {
        case OF_SAMI:
            sprintf ((char *) str,"</BODY></SAMI>\n");
            if (debug_608 && encoding!=ENC_UNICODE)
            {
                printf ("\r%s\n", str);
            }
            enc_buffer_used=encode_line (enc_buffer,(unsigned char *) str);
            fwrite (enc_buffer,enc_buffer_used,1,wb->fh);
            XMLRPC_APPEND(enc_buffer,enc_buffer_used);
            break;
        default: // Nothing to do. Only SAMI has a footer
            break;
    }
}


void write_subtitle_file_header (struct s_write *wb)
{
    switch (write_format)
    {
        case OF_SRT: // Subrip subtitles have no header
            break; 
        case OF_SAMI: // This header brought to you by McPoodle's CCASDI  
            //fprintf_encoded (wb->fh, sami_header);
            GUARANTEE(strlen (sami_header)*3);
            enc_buffer_used=encode_line (enc_buffer,(unsigned char *) sami_header);
            fwrite (enc_buffer,enc_buffer_used,1,wb->fh);
            XMLRPC_APPEND(enc_buffer,enc_buffer_used);
            break;
        case OF_RCWT: // Write header
            fwrite (rcwt_header, sizeof(rcwt_header),1,wb->fh);
            break;
        case OF_TRANSCRIPT: // No header. Fall thru
        default:
            break;
    }
}

void write_cc_line_as_transcript (struct eia608_screen *data, struct s_write *wb, int line_number)
{
    if (sentence_cap)
    {
        capitalize (line_number,data);
        correct_case(line_number,data);
    }
    int length = get_decoder_line_basic (subline, line_number, data);
    if (debug_608 && encoding!=ENC_UNICODE)
    {
        printf ("\r");
        printf ("%s\n",subline);
    }
    if (length>0)
    {
        fwrite (subline, 1, length, wb->fh);
        XMLRPC_APPEND(subline,length);
        fwrite (encoded_crlf, 1, encoded_crlf_length,wb->fh);
        XMLRPC_APPEND(encoded_crlf,encoded_crlf_length);    
    }
    // fprintf (wb->fh,encoded_crlf);
}

int write_cc_buffer_as_transcript (struct eia608_screen *data, struct s_write *wb)
{
    int wrote_something = 0;
    if (debug_608)
    {
        printf ("\n- - - TRANSCRIPT caption - - -\n");        
    }
    for (int i=0;i<15;i++)
    {
        if (data->row_used[i])
        {		
            write_cc_line_as_transcript (data,wb, i);
        }
        wrote_something=1;
    }
    if (debug_608)
    {
        printf ("- - - - - - - - - - - -\r\n");
    }
    return wrote_something;
}




struct eia608_screen *get_current_visible_buffer (struct s_write *wb)
{
    struct eia608_screen *data;
    if (wb->data608->visible_buffer==1)
        data = &wb->data608->buffer1;
    else
        data = &wb->data608->buffer2;
    return data;
}


int write_cc_buffer (struct s_write *wb)
{
    struct eia608_screen *data;
    int wrote_something=0;
    if (screens_to_process!=-1 && wb->data608->screenfuls_counter>=screens_to_process)
    {
        // We are done. 
        processed_enough=1;
        return 0;
    }
    if (wb->data608->visible_buffer==1)
        data = &wb->data608->buffer1;
    else
        data = &wb->data608->buffer2;

    if (!data->empty)
    {
        new_sentence=1;
        switch (write_format)
        {
            case OF_SRT:
                if (!startcredits_displayed && start_credits_text!=NULL)
                    try_to_add_start_credits(wb);            
                wrote_something = write_cc_buffer_as_srt (data, wb);
                break;
            case OF_SAMI:
                if (!startcredits_displayed && start_credits_text!=NULL)
                    try_to_add_start_credits(wb);
                wrote_something = write_cc_buffer_as_sami (data,wb);
                break;
            case OF_TRANSCRIPT:
                wrote_something = write_cc_buffer_as_transcript (data,wb);
                break;
            default: 
                break;
        }
        if (wrote_something)
            last_displayed_subs_ms=get_fts()+subs_delay; 

        if (wrote_something && gui_mode_reports)
            write_cc_buffer_to_gui (data,wb);
    }
    return wrote_something;
}

void roll_up(struct s_write *wb)
{
    eia608_screen *use_buffer;
    if (wb->data608->visible_buffer==1)
        use_buffer = &wb->data608->buffer1;
    else
        use_buffer = &wb->data608->buffer2;
    int keep_lines;
    switch (wb->data608->mode)
    {
        case MODE_ROLLUP_2:
            keep_lines=2;
            break;
        case MODE_ROLLUP_3:
            keep_lines=3;
            break;
        case MODE_ROLLUP_4:
            keep_lines=4;
            break;
        default: // Shouldn't happen
            keep_lines=0;
            break;
    }
    int firstrow=-1, lastrow=-1;
    // Look for the last line used
    int rows_now=0; // Number of rows in use right now
    for (int i=0;i<15;i++)
    {
        if (use_buffer->row_used[i])
        {
            rows_now++;
            if (firstrow==-1)
                firstrow=i;
            lastrow=i;
        }
    }
    
    if (debug_608)
        printf ("\rIn roll-up: %d lines used, first: %d, last: %d\n", rows_now, firstrow, lastrow);

    if (lastrow==-1) // Empty screen, nothing to rollup
        return;

    for (int j=lastrow-keep_lines+1;j<lastrow; j++)
    {
        if (j>=0)
        {
            memcpy (use_buffer->characters[j],use_buffer->characters[j+1],CC608_SCREEN_WIDTH+1);
            memcpy (use_buffer->colors[j],use_buffer->colors[j+1],CC608_SCREEN_WIDTH+1);
            memcpy (use_buffer->fonts[j],use_buffer->fonts[j+1],CC608_SCREEN_WIDTH+1);
            use_buffer->row_used[j]=use_buffer->row_used[j+1];
        }
    }
    for (int j=0;j<(1+wb->data608->cursor_row-keep_lines);j++)
    {
        memset(use_buffer->characters[j],' ',CC608_SCREEN_WIDTH);			
        memset(use_buffer->colors[j],COL_WHITE,CC608_SCREEN_WIDTH);
        memset(use_buffer->fonts[j],FONT_REGULAR,CC608_SCREEN_WIDTH);
        use_buffer->characters[j][CC608_SCREEN_WIDTH]=0;
        use_buffer->row_used[j]=0;
    }
    memset(use_buffer->characters[lastrow],' ',CC608_SCREEN_WIDTH);
    memset(use_buffer->colors[lastrow],COL_WHITE,CC608_SCREEN_WIDTH);
    memset(use_buffer->fonts[lastrow],FONT_REGULAR,CC608_SCREEN_WIDTH);

    use_buffer->characters[lastrow][CC608_SCREEN_WIDTH]=0;
    use_buffer->row_used[lastrow]=0;
    
    // Sanity check
    rows_now=0;
    for (int i=0;i<15;i++)
        if (use_buffer->row_used[i])
            rows_now++;
    if (rows_now>keep_lines)
        printf ("Bug in roll_up, should have %d lines but I have %d.\n",
            keep_lines, rows_now);
}

void erase_memory (struct s_write *wb, int displayed)
{
    eia608_screen *buf;
    if (displayed)
    {
        if (wb->data608->visible_buffer==1)
            buf=&wb->data608->buffer1;
        else
            buf=&wb->data608->buffer2;
    }
    else
    {
        if (wb->data608->visible_buffer==1)
            buf=&wb->data608->buffer2;
        else
            buf=&wb->data608->buffer1;
    }
    clear_eia608_cc_buffer (buf);
}

int is_current_row_empty (struct s_write *wb)
{
    eia608_screen *use_buffer;
    if (wb->data608->visible_buffer==1)
        use_buffer = &wb->data608->buffer1;
    else
        use_buffer = &wb->data608->buffer2;
    for (int i=0;i<CC608_SCREEN_WIDTH;i++)
    {
        if (use_buffer->characters[wb->data608->rollup_base_row][i]!=' ')
            return 0;
    }
    return 1;
}

/* Process GLOBAL CODES */
void handle_command (/*const */ unsigned char c1, const unsigned char c2, struct s_write *wb)
{
    // Handle channel change
    wb->data608->channel=new_channel;
    if (wb->data608->channel!=cc_channel)
        return;

    command_code command = COM_UNKNOWN;
    if (c1==0x15)
        c1=0x14;
    if ((c1==0x14 || c1==0x1C) && c2==0x2C)
        command = COM_ERASEDISPLAYEDMEMORY;
    if ((c1==0x14 || c1==0x1C) && c2==0x20)
        command = COM_RESUMECAPTIONLOADING;
    if ((c1==0x14 || c1==0x1C) && c2==0x2F)
        command = COM_ENDOFCAPTION;
    if ((c1==0x17 || c1==0x1F) && c2==0x21)
        command = COM_TABOFFSET1;
    if ((c1==0x17 || c1==0x1F) && c2==0x22)
        command = COM_TABOFFSET2;
    if ((c1==0x17 || c1==0x1F) && c2==0x23)
        command = COM_TABOFFSET3;
    if ((c1==0x14 || c1==0x1C) && c2==0x25)
        command = COM_ROLLUP2;
    if ((c1==0x14 || c1==0x1C) && c2==0x26)
        command = COM_ROLLUP3;
    if ((c1==0x14 || c1==0x1C) && c2==0x27)
        command = COM_ROLLUP4;
    if ((c1==0x14 || c1==0x1C) && c2==0x2D)
        command = COM_CARRIAGERETURN;
    if ((c1==0x14 || c1==0x1C) && c2==0x2E)
        command = COM_ERASENONDISPLAYEDMEMORY;
    if ((c1==0x14 || c1==0x1C) && c2==0x21)
        command = COM_BACKSPACE;
    if ((c1==0x14 || c1==0x1C) && c2==0x2b)
        command = COM_RESUMETEXTDISPLAY;
    if (debug_608)
    {
        printf ("\rCommand: %02X %02X (%s)\n",c1,c2,command_type[command]);
    }
    switch (command)
    {
        case COM_BACKSPACE:
            if (wb->data608->cursor_column>0)
            {
                wb->data608->cursor_column--;
                get_writing_buffer(wb)->characters[wb->data608->cursor_row][wb->data608->cursor_column]=' ';
            }
            break;
        case COM_TABOFFSET1:
            if (wb->data608->cursor_column<31)
                wb->data608->cursor_column++;
            break;
        case COM_TABOFFSET2:
            wb->data608->cursor_column+=2;
            if (wb->data608->cursor_column>31)
                wb->data608->cursor_column=31;
            break;
        case COM_TABOFFSET3:
            wb->data608->cursor_column+=3;
            if (wb->data608->cursor_column>31)
                wb->data608->cursor_column=31;
            break;
        case COM_RESUMECAPTIONLOADING:
            wb->data608->mode=MODE_POPUP;
            break;
        case COM_RESUMETEXTDISPLAY:
            wb->data608->mode=MODE_TEXT;
            break;
        case COM_ROLLUP2:            
            if (wb->data608->mode==MODE_POPUP)
            {
                if (write_cc_buffer (wb))
                    wb->data608->screenfuls_counter++;
                erase_memory (wb, true);			
            }
            if (wb->data608->mode==MODE_ROLLUP_2 && !is_current_row_empty(wb))
            {
                if (debug_608)
                    printf ("Two RU2, current line not empty. Simulating a CR\n");
                handle_command(0x14, 0x2D, wb);
            }   
            wb->data608->mode=MODE_ROLLUP_2;
            erase_memory (wb, false);
            wb->data608->cursor_column=0;
            wb->data608->cursor_row=wb->data608->rollup_base_row;
            break;
        case COM_ROLLUP3:
            if (wb->data608->mode==MODE_POPUP)
            {
                if (write_cc_buffer (wb))
                    wb->data608->screenfuls_counter++;
                erase_memory (wb, true);
            }
            if (wb->data608->mode==MODE_ROLLUP_3 && !is_current_row_empty(wb))
            {
                if (debug_608)
                    printf ("Two RU3, current line not empty. Simulating a CR\n");
                handle_command(0x14, 0x2D, wb);
            }
            wb->data608->mode=MODE_ROLLUP_3;
            erase_memory (wb, false);
            wb->data608->cursor_column=0;
            wb->data608->cursor_row=wb->data608->rollup_base_row;
            break;
        case COM_ROLLUP4:
            if (wb->data608->mode==MODE_POPUP)
            {
                if (write_cc_buffer (wb))
                    wb->data608->screenfuls_counter++;
                erase_memory (wb, true);			
            }
            if (wb->data608->mode==MODE_ROLLUP_4 && !is_current_row_empty(wb))
            {
                if (debug_608)
                    printf ("Two RU4, current line not empty. Simulating a CR\n");
                handle_command(0x14, 0x2D, wb);
            }
            
            wb->data608->mode=MODE_ROLLUP_4;
            wb->data608->cursor_column=0;
            wb->data608->cursor_row=wb->data608->rollup_base_row;
            erase_memory (wb, false);
            break;
        case COM_CARRIAGERETURN:
            // In transcript mode, CR doesn't write the whole screen, to avoid
            // repeated lines.
            if (write_format==OF_TRANSCRIPT)
            {
                write_cc_line_as_transcript(get_current_visible_buffer (wb), wb, wb->data608->cursor_row);
            }
            else
            {
                if (norollup)
                    delete_all_lines_but_current (get_current_visible_buffer (wb), wb->data608->cursor_row);
                if (write_cc_buffer(wb))
                    wb->data608->screenfuls_counter++;
            }
            roll_up(wb);		
            wb->data608->current_visible_start_ms=get_fts();
            wb->data608->cursor_column=0;
            break;
        case COM_ERASENONDISPLAYEDMEMORY:
            erase_memory (wb,false);
            break;
        case COM_ERASEDISPLAYEDMEMORY:
            // Write it to disk before doing this, and make a note of the new
            // time it became clear.
            if (write_format==OF_TRANSCRIPT && 
                (wb->data608->mode==MODE_ROLLUP_2 || wb->data608->mode==MODE_ROLLUP_3 ||
                wb->data608->mode==MODE_ROLLUP_4))
            {
                // In transcript mode we just write the cursor line. The previous lines
                // should have been written already, so writing everything produces
                // duplicate lines.                
                write_cc_line_as_transcript(get_current_visible_buffer (wb), wb, wb->data608->cursor_row);
            }
            else
            {
                if (write_cc_buffer (wb))
                    wb->data608->screenfuls_counter++;
            }
            erase_memory (wb,true);
            wb->data608->current_visible_start_ms=get_fts();
            break;
        case COM_ENDOFCAPTION: // Switch buffers
            // The currently *visible* buffer is leaving, so now we know it's ending
            // time. Time to actually write it to file.
            if (write_cc_buffer (wb))
                wb->data608->screenfuls_counter++;
            wb->data608->visible_buffer = (wb->data608->visible_buffer==1) ? 2 : 1;
            wb->data608->current_visible_start_ms=get_fts();
            wb->data608->cursor_column=0;
            wb->data608->cursor_row=0;
            wb->data608->color=default_color;
            wb->data608->font=FONT_REGULAR;
            break;
        default:
            if (debug_608)
            {
                printf ("\rNot yet implemented.\n");
            }
            break;
    }
}

void handle_end_of_data (struct s_write *wb)
{
    // We issue a EraseDisplayedMemory here so if there's any captions pending
    // they get written to file. 
    handle_command (0x14, 0x2c, wb); // EDM
}

void handle_double (const unsigned char c1, const unsigned char c2, struct s_write *wb)
{
    unsigned char c;
    if (wb->data608->channel!=cc_channel)
        return;
    if (c2>=0x30 && c2<=0x3f)
    {
        c=c2 + 0x50; // So if c>=0x80 && c<=0x8f, it comes from here
        if (debug_608)
            printf ("\rDouble: %02X %02X  -->  %c\n",c1,c2,c);
        write_char(c,wb);
    }
}

/* Process EXTENDED CHARACTERS */
unsigned char handle_extended (unsigned char hi, unsigned char lo, struct s_write *wb)
{
    // Handle channel change
    if (new_channel > 2) 
    {
        new_channel -= 2;
        if (debug_608)
            printf ("\nChannel correction, now %d\n", new_channel);
    }
    wb->data608->channel=new_channel;
    if (wb->data608->channel!=cc_channel)
        return 0;

    // For lo values between 0x20-0x3f
    unsigned char c=0;

    if (debug_608)
        printf ("\rExtended: %02X %02X\n",hi,lo);
    if (lo>=0x20 && lo<=0x3f && (hi==0x12 || hi==0x13))
    {
        switch (hi)
        {
            case 0x12:
                c=lo+0x70; // So if c>=0x90 && c<=0xaf it comes from here
                break;
            case 0x13:
                c=lo+0x90; // So if c>=0xb0 && c<=0xcf it comes from here
                break;
        }
        // This column change is because extended characters replace 
        // the previous character (which is sent for basic decoders
        // to show something similar to the real char)
        if (wb->data608->cursor_column>0)        
            wb->data608->cursor_column--;        

        write_char (c,wb);
    }
    return 1;
}

/* Process PREAMBLE ACCESS CODES (PAC) */
void handle_pac (unsigned char c1, unsigned char c2, struct s_write *wb)
{
    // Handle channel change
    if (new_channel > 2) 
    {
        new_channel -= 2;
        if (debug_608)
            printf ("\nChannel correction, now %d\n", new_channel);
    }
    wb->data608->channel=new_channel;
    if (wb->data608->channel!=cc_channel)
        return;

    int row=rowdata[((c1<<1)&14)|((c2>>5)&1)];

    if (debug_608)
        printf ("\rPAC: %02X %02X",c1,c2);

    if (c2>=0x40 && c2<=0x5f)
    {
        c2=c2-0x40;
    }
    else
    {
        if (c2>=0x60 && c2<=0x7f)
        {
            c2=c2-0x60;
        }
        else
        {
            if (debug_608)
                printf ("\rThis is not a PAC!!!!!\n");
            return;
        }
    }
    int color=pac2_attribs[c2][0];
    int font=pac2_attribs[c2][1];
    int indent=pac2_attribs[c2][2];
    if (debug_608)
        printf ("  --  Position: %d:%d, color: %s,  font: %s\n",row,
        indent,color_text[color][0],font_text[font]);
    if (wb->data608->mode!=MODE_TEXT)
    {
        // According to Robson, row info is discarded in text mode
        // but column is accepted
        wb->data608->cursor_row=row-1 ; // Since the array is 0 based
    }
    wb->data608->rollup_base_row=row-1;
    wb->data608->cursor_column=indent;	
}


void handle_single (const unsigned char c1, struct s_write *wb)
{	
    if (c1<0x20 || wb->data608->channel!=cc_channel)
        return; // We don't allow special stuff here
    if (debug_608)
        printf ("%c",c1);

    /*if (debug_608)
    printf ("Character: %02X (%c) -> %02X (%c)\n",c1,c1,c,c); */
    write_char (c1,wb);
}

int check_channel (unsigned char c1, struct s_write *wb)
{
    if (c1==0x14) 
    {
        if (debug_608 && wb->data608->channel!=1)
            printf ("\nChannel change, now 1\n");
        return 1;
    }
    if (c1==0x1c) 
    {
        if (debug_608 && wb->data608->channel!=2)
            printf ("\nChannel change, now 2\n");
        return 2;
    }
    if (c1==0x15) 
    {
        if (debug_608 && wb->data608->channel!=3)
            printf ("\nChannel change, now 3\n");
        return 3;
    }
    if (c1==0x1d) 
    {
        if (debug_608 && wb->data608->channel!=4)
            printf ("\nChannel change, now 4\n");
        return 4;
    }

    // Otherwise keep the current channel
    return wb->data608->channel;
}

/* Handle Command, special char or attribute and also check for
* channel changes.
* Returns 1 if something was written to screen, 0 otherwise */
int disCommand (unsigned char hi, unsigned char lo, struct s_write *wb)
{
    int wrote_to_screen=0;

    /* Full channel changes are only allowed for "GLOBAL CODES",
    * "OTHER POSITIONING CODES", "BACKGROUND COLOR CODES",
    * "MID-ROW CODES".
    * "PREAMBLE ACCESS CODES", "BACKGROUND COLOR CODES" and
    * SPECIAL/SPECIAL CHARACTERS allow only switching
    * between 1&3 or 2&4. */
    new_channel = check_channel (hi,wb);
    //if (wb->data608->channel!=cc_channel)
    //    continue;

    if (hi>=0x18 && hi<=0x1f)
        hi=hi-8;

    switch (hi)
    {
        case 0x10:
            if (lo>=0x40 && lo<=0x5f)
                handle_pac (hi,lo,wb);
            break;
        case 0x11:
            if (lo>=0x20 && lo<=0x2f)
                handle_text_attr (hi,lo,wb);
            if (lo>=0x30 && lo<=0x3f)
            {
                wrote_to_screen=1;
                handle_double (hi,lo,wb);
            }
            if (lo>=0x40 && lo<=0x7f)
                handle_pac (hi,lo,wb);
            break;
        case 0x12:
        case 0x13:
            if (lo>=0x20 && lo<=0x3f)
            {
                wrote_to_screen=handle_extended (hi,lo,wb);
            }
            if (lo>=0x40 && lo<=0x7f)
                handle_pac (hi,lo,wb);
            break;
        case 0x14:
        case 0x15:
            if (lo>=0x20 && lo<=0x2f)
                handle_command (hi,lo,wb);
            if (lo>=0x40 && lo<=0x7f)
                handle_pac (hi,lo,wb);
            break;
        case 0x16:
            if (lo>=0x40 && lo<=0x7f)
                handle_pac (hi,lo,wb);
            break;
        case 0x17:
            if (lo>=0x21 && lo<=0x22)
                handle_command (hi,lo,wb);
            if (lo>=0x2e && lo<=0x2f)
                handle_text_attr (hi,lo,wb);
            if (lo>=0x40 && lo<=0x7f)
                handle_pac (hi,lo,wb);
            break;
    }
    return wrote_to_screen;
}

void process608 (const unsigned char *data, int length, struct s_write *wb)
{
    static int textprinted = 0;

    if (data!=NULL)
    {
        for (int i=0;i<length;i=i+2)
        {
            unsigned char hi, lo;
            int wrote_to_screen=0; 
            hi = data[i] & 0x7F; // Get rid of parity bit
            lo = data[i+1] & 0x7F; // Get rid of parity bit

            if (hi==0 && lo==0) // Just padding
                continue;
            // printf ("\r[%02X:%02X]\n",hi,lo);

            if (hi>=0x01 && hi<=0x0E)
            {
                // XDS crap - mode. Would be nice to support it eventually
                // wb->data608->last_c1=0;
                // wb->data608->last_c2=0;
                wb->data608->channel=3; // Always channel 3
                in_xds_mode=1;
            }
            if (hi==0x0F) // End of XDS block
            {
                in_xds_mode=0;
                continue;
            }
            if (hi>=0x10 && hi<0x1F) // Non-character code or special/extended char
                // http://www.geocities.com/mcpoodle43/SCC_TOOLS/DOCS/CC_CODES.HTML
                // http://www.geocities.com/mcpoodle43/SCC_TOOLS/DOCS/CC_CHARS.HTML
            {
                // We were writing characters before, start a new line for
                // diagnostic output from disCommand()
                if (debug_608 && textprinted == 1 )
                {
                    printf("\n");
                    textprinted = 0;
                }

                in_xds_mode=0; // Back to normal
                if (wb->data608->last_c1==hi && wb->data608->last_c2==lo)
                {
                    // Duplicate dual code, discard
                    continue;
                }
                wb->data608->last_c1=hi;
                wb->data608->last_c2=lo;
                wrote_to_screen=disCommand (hi,lo,wb);
            }
            if (hi>=0x20) // Standard characters (always in pairs)
            {
                // Only print if the channel is active
                if (wb->data608->channel!=cc_channel)
                    continue;

                if (debug_608)
                {
                    if( textprinted == 0 )
                    {
                        printf("\n");
                        textprinted = 1;
                    }
                }

                handle_single(hi,wb);
                handle_single(lo,wb);
                wrote_to_screen=1;
                wb->data608->last_c1=0;
                wb->data608->last_c2=0;
            }

            if ( debug_608 && !textprinted && wb->data608->channel==cc_channel )
            {   // Current FTS information after the characters are shown
                printf("Current FTS: %s\n", print_mstime(get_fts()));
            }

            if (wrote_to_screen && direct_rollup && // If direct_rollup is enabled and
                (wb->data608->mode==MODE_ROLLUP_2 || // we are in rollup mode, write now.
                wb->data608->mode==MODE_ROLLUP_3 ||
                wb->data608->mode==MODE_ROLLUP_4))
            {
                // We don't increase screenfuls_counter here.
                write_cc_buffer (wb);
                wb->data608->current_visible_start_ms=get_fts();
            }
        }
    }
}


/* Return a pointer to a string that holds the printable characters
 * of the caption data block. FOR DEBUG PURPOSES ONLY! */
unsigned char *debug_608toASC (unsigned char *cc_data, int channel)
{
    static unsigned char output[3];

    unsigned char cc_valid = (cc_data[0] & 4) >>2;
    unsigned char cc_type = cc_data[0] & 3;
    unsigned char hi, lo;

    output[0]=' ';
    output[1]=' ';
    output[2]='\x00';

    if (cc_valid && cc_type==channel)
    {
        hi = cc_data[1] & 0x7F; // Get rid of parity bit
        lo = cc_data[2] & 0x7F; // Get rid of parity bit
        if (hi>=0x20)
        {
            output[0]=hi;
            output[1]=(lo>=20 ? lo : '.');
            output[2]='\x00';
        }
        else
        {
            output[0]='<';
            output[1]='>';
            output[2]='\x00';
        }
    }
    return output;
}
