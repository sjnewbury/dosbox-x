/*
 *  Copyright (C) 2002-2015  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "dosbox.h"
#include "logging.h"
#include "support.h"
#include "control.h"
#include "regs.h"
#include "debug.h"
#include "debug_inc.h"

_LogGroup loggrp[LOG_MAX]={{"",LOG_NORMAL},{0,LOG_NORMAL}};
FILE* debuglog = NULL;

#if C_DEBUG
#include <curses.h>

#include <list>
#include <string>
using namespace std;

#define MAX_LOG_BUFFER 500
static list<string> logBuff;
static list<string>::iterator logBuffPos = logBuff.end();

extern int old_cursor_state;

void DEBUG_RefreshPage(char scroll) {
	if (scroll==-1 && logBuffPos!=logBuff.begin()) logBuffPos--;
	else if (scroll==1 && logBuffPos!=logBuff.end()) logBuffPos++;

	list<string>::iterator i = logBuffPos;
	int maxy, maxx; getmaxyx(dbg.win_out,maxy,maxx);
	int rem_lines = maxy - 1;
	if(rem_lines == -1) return;

	wclear(dbg.win_out);
	while (rem_lines > 0 && i!=logBuff.begin()) {
		--i;
		/* Const cast is needed for pdcurses which has no const char in mvwprintw (bug maybe) */
		mvwprintw(dbg.win_out,rem_lines, 0, const_cast<char*>((*i).c_str()));
		rem_lines--;
	}
	wrefresh(dbg.win_out);
}

static void Draw_RegisterLayout(void) {
	mvwaddstr(dbg.win_reg,0,0,"EAX=");
	mvwaddstr(dbg.win_reg,1,0,"EBX=");
	mvwaddstr(dbg.win_reg,2,0,"ECX=");
	mvwaddstr(dbg.win_reg,3,0,"EDX=");

	mvwaddstr(dbg.win_reg,0,14,"ESI=");
	mvwaddstr(dbg.win_reg,1,14,"EDI=");
	mvwaddstr(dbg.win_reg,2,14,"EBP=");
	mvwaddstr(dbg.win_reg,3,14,"ESP=");

	mvwaddstr(dbg.win_reg,0,28,"DS=");
	mvwaddstr(dbg.win_reg,0,38,"ES=");
	mvwaddstr(dbg.win_reg,0,48,"FS=");
	mvwaddstr(dbg.win_reg,0,58,"GS=");
	mvwaddstr(dbg.win_reg,0,68,"SS=");

	mvwaddstr(dbg.win_reg,1,28,"CS=");
	mvwaddstr(dbg.win_reg,1,38,"EIP=");

	mvwaddstr(dbg.win_reg,2,75,"CPL");
	mvwaddstr(dbg.win_reg,2,68,"IOPL");

	mvwaddstr(dbg.win_reg,1,52,"C  Z  S  O  A  P  D  I  T ");
}


static void DrawBars(void) {
	if (has_colors()) {
		attrset(COLOR_PAIR(PAIR_BLACK_BLUE));
	}
	/* Show the Register bar */
	mvaddstr(1-1,0, "---(Register Overview                   )---");
	/* Show the Data Overview bar perhaps with more special stuff in the end */
	mvaddstr(6-1,0,"---(Data Overview   Scroll: page up/down)---");
	/* Show the Code Overview perhaps with special stuff in bar too */
	mvaddstr(17-1,0,"---(Code Overview   Scroll: up/down     )---");
	/* Show the Variable Overview bar */
	mvaddstr(29-1,0, "---(Variable Overview                   )---");
	/* Show the Output OverView */
	mvaddstr(34-1,0, "---(Output          Scroll: home/end    )---");
	attrset(0);
	//Match values with below. So we don't need to touch the internal window structures
}



static void MakeSubWindows(void) {
	/* The Std output win should go at the bottom */
	/* Make all the subwindows */
	int win_main_maxy, win_main_maxx; getmaxyx(dbg.win_main,win_main_maxy,win_main_maxx);
	int outy=1; //Match values with above
	/* The Register window  */
	dbg.win_reg=subwin(dbg.win_main,4,win_main_maxx,outy,0);
	outy+=5; // 6
	/* The Data Window */
	dbg.win_data=subwin(dbg.win_main,10,win_main_maxx,outy,0);
	outy+=11; // 17
	/* The Code Window */
	dbg.win_code=subwin(dbg.win_main,11,win_main_maxx,outy,0);
	outy+=12; // 29
	/* The Variable Window */
	dbg.win_var=subwin(dbg.win_main,4,win_main_maxx,outy,0);
	outy+=5; // 34
	/* The Output Window */	
	dbg.win_out=subwin(dbg.win_main,win_main_maxy-outy-2,win_main_maxx,outy,0);
	if(!dbg.win_reg ||!dbg.win_data || !dbg.win_code || !dbg.win_var || !dbg.win_out) E_Exit("Setting up windows failed");
//	dbg.input_y=win_main_maxy-1;
	scrollok(dbg.win_out,TRUE);
	DrawBars();
	Draw_RegisterLayout();
	refresh();
}

static void MakePairs(void) {
	init_pair(PAIR_BLACK_BLUE, COLOR_BLACK, COLOR_CYAN);
	init_pair(PAIR_BYELLOW_BLACK, COLOR_YELLOW /*| FOREGROUND_INTENSITY */, COLOR_BLACK);
	init_pair(PAIR_GREEN_BLACK, COLOR_GREEN /*| FOREGROUND_INTENSITY */, COLOR_BLACK);
	init_pair(PAIR_BLACK_GREY, COLOR_BLACK /*| FOREGROUND_INTENSITY */, COLOR_WHITE);
	init_pair(PAIR_GREY_RED, COLOR_WHITE/*| FOREGROUND_INTENSITY */, COLOR_RED);
}

void DBGUI_StartUp(void) {
	/* Start the main window */
	dbg.win_main=initscr();
	cbreak();       /* take input chars one at a time, no wait for \n */
	noecho();       /* don't echo input */
	scrollok(stdscr,false);
	nodelay(dbg.win_main,true);
	keypad(dbg.win_main,true);
	#ifndef WIN32
	printf("\e[8;50;80t");
	fflush(NULL);
	resizeterm(50,80);
	touchwin(dbg.win_main);
	#endif
	old_cursor_state = curs_set(0);
	start_color();
	cycle_count=0;
	MakePairs();
	MakeSubWindows();
}

#endif

void DEBUG_ShowMsg(char const* format,...) {
	char buf[512];
	va_list msg;
	size_t len;

	va_start(msg,format);
	len = vsnprintf(buf,sizeof(buf)-2,format,msg); /* <- NTS: Did you know sprintf/vsnprintf returns number of chars written? */
	va_end(msg);

	/* Add newline if not present */
	if (len > 0 && buf[len-1] != '\n') buf[len++] = '\n';
	buf[len] = 0;

	if (debuglog != NULL) {
		fprintf(debuglog,"%s",buf);
		fflush(debuglog);
	}
#if !C_DEBUG
	else {
		fprintf(stderr,"DOSBox LOG: %s",buf);
		fflush(stderr);
	}
#endif

#if C_DEBUG
	if (logBuffPos!=logBuff.end()) {
		logBuffPos=logBuff.end();
		DEBUG_RefreshPage(0);
	}
	logBuff.push_back(buf);
	if (logBuff.size() > MAX_LOG_BUFFER)
		logBuff.pop_front();

	logBuffPos = logBuff.end();
	wprintw(dbg.win_out,"%s",buf);
	wrefresh(dbg.win_out);
#endif
}

void LOG_Destroy(Section*) {
	if (debuglog != NULL) {
		fclose(debuglog);
		debuglog = NULL;
	}
}

void Null_Init(Section *sec);

void LOG::operator() (char const* format, ...){
	const char *s_severity = "";
	char buf[512];
	va_list msg;

	switch (d_severity) {
		case LOG_DEBUG:	s_severity = " DEBUG"; break;
		case LOG_NORMAL:s_severity = "      "; break;
		case LOG_WARN:  s_severity = " WARN "; break;
		case LOG_ERROR: s_severity = " ERROR"; break;
		case LOG_FATAL: s_severity = " FATAL"; break;
		default: break;
	};

	va_start(msg,format);
	vsnprintf(buf,sizeof(buf)-1,format,msg);
	va_end(msg);

	if (d_type>=LOG_MAX) return;
	if (d_severity < loggrp[d_type].min_severity) return;
	DEBUG_ShowMsg("%10u%s %s:%s\n",static_cast<Bit32u>(cycle_count),s_severity,loggrp[d_type].front,buf);
}

void LOG_ParseEnableSetting(_LogGroup &group,const char *setting) {
	if (!strcmp(setting,"true") || !strcmp(setting,"1") || !strcmp(setting,"normal"))
		group.min_severity = LOG_NORMAL; /* original code's handling is equivalent to our "normal" setting */
	else if (!strcmp(setting,"false") || !strcmp(setting,"0") || !strcmp(setting,""))
		group.min_severity = LOG_ERROR; /* original code's handling is equivalent to our "error" setting */
	else if (!strcmp(setting,"debug"))
		group.min_severity = LOG_DEBUG;
	else if (!strcmp(setting,"warn"))
		group.min_severity = LOG_WARN;
	else if (!strcmp(setting,"error"))
		group.min_severity = LOG_ERROR;
	else if (!strcmp(setting,"fatal"))
		group.min_severity = LOG_FATAL;
	else if (!strcmp(setting,"never"))
		group.min_severity = LOG_NEVER;
	else
		group.min_severity = LOG_NORMAL;
}

void LOG_Init(Section * sec) {
	Section_prop * sect=static_cast<Section_prop *>(sec);
	const char * blah=sect->Get_string("logfile");
	if (blah != NULL && blah[0] != 0 && (debuglog=fopen(blah,"wt+")) != NULL) {
		setbuf(debuglog,NULL);
	}
	else {
		debuglog=0;
	}

	sect->AddDestroyFunction(&LOG_Destroy);
	char buf[1024];
	for (Bitu i=1;i<LOG_MAX;i++) {
		strcpy(buf,loggrp[i].front);
		buf[strlen(buf)]=0;
		lowcase(buf);

		if (control->opt_debug)
			LOG_ParseEnableSetting(/*&*/loggrp[i],"debug");
		else
			LOG_ParseEnableSetting(/*&*/loggrp[i],sect->Get_string(buf));
	}
}

void LOG_StartUp(void) {
	const char *log_values[] = {
		/* compatibility with existing dosbox.conf files */
		"true", "false",

		/* log levels */
		"debug",
		"normal",
		"warn",
		"error",
		"fatal",
		"never",		/* <- this means NEVER EVER log anything */

		0};

	/* Setup logging groups */
	loggrp[LOG_ALL].front="ALL";
	loggrp[LOG_VGA].front="VGA";
	loggrp[LOG_VGAGFX].front="VGAGFX";
	loggrp[LOG_VGAMISC].front="VGAMISC";
	loggrp[LOG_INT10].front="INT10";
	loggrp[LOG_SB].front="SBLASTER";
	loggrp[LOG_DMACONTROL].front="DMA_CONTROL";
	
	loggrp[LOG_FPU].front="FPU";
	loggrp[LOG_CPU].front="CPU";
	loggrp[LOG_PAGING].front="PAGING";

	loggrp[LOG_FCB].front="FCB";
	loggrp[LOG_FILES].front="FILES";
	loggrp[LOG_IOCTL].front="IOCTL";
	loggrp[LOG_EXEC].front="EXEC";
	loggrp[LOG_DOSMISC].front="DOSMISC";

	loggrp[LOG_PIT].front="PIT";
	loggrp[LOG_KEYBOARD].front="KEYBOARD";
	loggrp[LOG_PIC].front="PIC";

	loggrp[LOG_MOUSE].front="MOUSE";
	loggrp[LOG_BIOS].front="BIOS";
	loggrp[LOG_GUI].front="GUI";
	loggrp[LOG_MISC].front="MISC";

	loggrp[LOG_IO].front="IO";
	loggrp[LOG_PCI].front="PCI";
	
	loggrp[LOG_VOODOO].front="SST";
	
	/* Register the log section */
	Section_prop * sect=control->AddSection_prop("log",Null_Init);
	Prop_string* Pstring = sect->Add_string("logfile",Property::Changeable::Always,"");
	Pstring->Set_help("file where the log messages will be saved to");
	char buf[1024];
	for (Bitu i=1;i<LOG_MAX;i++) {
		strcpy(buf,loggrp[i].front);
		lowcase(buf);

		Pstring = sect->Add_string(buf,Property::Changeable::Always,"false");
		Pstring->Set_values(log_values);
		Pstring->Set_help("Enable/Disable logging of this type.");
	}
}

