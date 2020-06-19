
/*
 *  Copyright (C) 2020 Mayco S. Berghetti
 *
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <string.h>  // strlen
#include <ncurses.h>

#include "str.h"
#include "process.h"
#include "conection.h"
#include "color.h"
#include "translate.h"
#include "show.h"
#include "sort.h"

// defined un terminal.c
extern WINDOW *pad;

// tamanho representação textual porta da camada de transporte
#define PORTLEN 5  // 65535

// "ddd.ddd.ddd.ddd:ppppp <-> ddd.ddd.ddd.ddd:ppppp"
#define LEN_TUPLE ( ( INET_ADDRSTRLEN + PORTLEN ) * 2 ) + 7

// espaçamento entre as colunas
#define PID -5  // negativo alinhado a esquerda
#define PPS 6
#define RATE 13

// espaçamento da estatistica até a tupla
#define TUPLE 29

// armazina a linha selecionada com seus atributos antes de estar "selecionada"
static chtype line_original[COLS_PAD] = {0};

static int sort_by = RATE_RX;  // ordenação padrão
static int scroll_x = 0;
static int scroll_y = 1;
static int selected = 1;  // posição de linha do item selecionado
static int tot_rows;      // total linhas exibidas

// static chtype line_color[COLS_PAD];  // versão otimizada não precisa

static void
show_conections ( const process_t *restrict process, const struct config_op *restrict co );

static void
show_header (const struct config_op *co);

static void
paint_selected (const struct config_op *co);

void
start_ui ( const struct config_op *co )
{
  show_header (co);
  doupdate ();
}

void
show_process ( const process_t *restrict processes, const size_t tot_process, const struct config_op * restrict co )
{
  int len_base_name, len_name;
  // limpa a tela e o scrollback
  // wclear ( pad );

  // show_header ();

  tot_rows = 0;

  sort ( ( process_t * ) processes, tot_process, sort_by );

  wmove ( pad, 1, 0 );  // move second line
  for ( size_t i = 0; i < tot_process; i++ )
    {
      // só exibe o processo se tiver fluxo de rede
      if ( processes[i].net_stat.tot_Bps_rx ||
           processes[i].net_stat.tot_Bps_tx )
        {
          tot_rows++;
          wprintw ( pad,
                    " %*d %*ld %*ld ",
                    PID,
                    processes[i].pid,
                    PPS,
                    processes[i].net_stat.avg_pps_tx,
                    PPS,
                    processes[i].net_stat.avg_pps_rx );

          wprintw ( pad,
                    "%*s %*s %*s %*s ",
                    RATE,
                    processes[i].net_stat.tx_rate,
                    RATE,
                    processes[i].net_stat.rx_rate,
                    RATE,
                    processes[i].net_stat.tx_tot,
                    RATE,
                    processes[i].net_stat.rx_tot );

          // wprintw ( pad, "%s", processes[i].name );

          // /usr/bin/programa-nome
          len_base_name = strlen_space ( processes[i].name );

          // programa-nome
          len_name =
                  find_last_char ( processes[i].name, len_base_name, '/' ) + 1;

          for ( int j = 0; processes[i].name[j]; j++ )
            {
              if ( j >= len_name && j < len_base_name )
                // pinta somente o nome do programa
                waddch ( pad,
                         processes[i].name[j] | co->color_scheme[NAME_PROG_BOLD] );
              else
                // pinta todo o caminho do programa e parametros
                waddch ( pad, processes[i].name[j] | co->color_scheme[NAME_PROG] );
            }

          waddch ( pad, '\n' );

          if ( co->view_conections & ( processes[i].net_stat.avg_Bps_rx ||
                                   processes[i].net_stat.avg_Bps_tx ) )
            show_conections ( &processes[i], co );
        }
    }

  // clear lines begin cursor end screen, replace wclear()
  wclrtobot ( pad );

  // paint item SELECTED_H
  if ( tot_rows )
    {
      if ( selected > tot_rows )
        selected = tot_rows;

      // salva conteudo da linha antes de pintar
      mvwinchnstr ( pad, selected, 0, line_original, COLS_PAD - 1 );
      // wprintw(pad, "vamo ve 2 \n%d\n%d\n", tot_rows, SELECTED_H);
      // (re)pinta item selecionado
      paint_selected (co);
    }

  // pnoutrefresh ( pad, 0, scroll_x, 0, 0, 0, COLS - 1 );  // atualiza
  // cabeçalho

  // prefresh ( pad, scroll_y, scroll_x, 1, 0, LINES - 1, COLS - 1 );
  // pnoutrefresh ( pad, 0, 0, 1, 0, LINES - 1, COLS - 1 );
  pnoutrefresh ( pad, scroll_y, scroll_x, 1, 0, LINES - 1, COLS - 1 );

  // depois de todas as janelas verificadas, atualiza todas uma unica vez
  doupdate ();
}

static void
show_conections ( const process_t *restrict process, const struct config_op *restrict co )
{
  // tuple ip:port <-> ip:port
  char *tuple;
  bool last_con = false;
  size_t i;

  wattron ( pad, co->color_scheme[CONECTIONS] );
  for ( i = 0; i < process->total_conections; i++ )
    {
      tot_rows++;

      // se a proxima conexão estiver com estatisticas zeradas, essa é a ultima
      // conexão, as conexões são ordenadas de forma decrescente previamente
      if ( ( i < process->total_conections - 1 &&
             process->conection[i + 1].net_stat.avg_Bps_rx == 0 &&
             process->conection[i + 1].net_stat.avg_Bps_tx == 0 &&
             process->conection[i + 1].net_stat.tot_Bps_rx == 0 &&
             process->conection[i + 1].net_stat.tot_Bps_rx == 0 ) ||
           i == process->total_conections - 1 )
        last_con = true;

      // faz a tradução de ip:porta para nome-reverso:serviço
      tuple = translate ( &process->conection[i], co );

      wprintw ( pad,
                " %*s %*ld %*ld %*s %*s ",
                PID,
                "",
                PPS,
                process->conection[i].net_stat.avg_pps_tx,
                PPS,
                process->conection[i].net_stat.avg_pps_rx,
                RATE,
                process->conection[i].net_stat.tx_rate,
                RATE,
                process->conection[i].net_stat.rx_rate );

      wprintw ( pad, "%*s", TUPLE, "" );

      wattron ( pad, co->color_scheme[TREE] );
      if ( !last_con )
        {
          waddch ( pad, ACS_LTEE );   // ├
          waddch ( pad, ACS_HLINE );  // ─
        }
      else
        {                                // ultima conexão
          waddch ( pad, ACS_LLCORNER );  // └
          waddch ( pad, ACS_HLINE );     // ─
        }
      wattroff ( pad, co->color_scheme[TREE] );

      wprintw ( pad, " %s\n", tuple );

      if ( last_con )
        break;
    }
  // se teve conexões exibidas
  if ( last_con )
    {
      waddch ( pad, '\n' );
      tot_rows++;
    }

  wattroff ( pad, co->color_scheme[CONECTIONS] );
}

static void
show_header ( const struct config_op *co )
{
  wmove ( pad, 0, 0 );  // move first line

  wattrset ( pad,
             ( sort_by == S_PID ) ? co->color_scheme[SELECTED_H]
                                  : co->color_scheme[HEADER] );
  wprintw ( pad, " %*s ", PID, "PID" );

  wattrset ( pad,
             ( sort_by == PPS_TX ) ? co->color_scheme[SELECTED_H]
                                   : co->color_scheme[HEADER] );
  wprintw ( pad, "%*s ", PPS, "PPS TX" );

  wattrset ( pad,
             ( sort_by == PPS_RX ) ? co->color_scheme[SELECTED_H]
                                   : co->color_scheme[HEADER] );
  wprintw ( pad, "%*s", PPS, "PPS RX" );

  wattrset ( pad,
             ( sort_by == RATE_TX ) ? co->color_scheme[SELECTED_H]
                                    : co->color_scheme[HEADER] );
  wprintw ( pad, "    %s   ", "RATE TX" );

  wattrset ( pad,
             ( sort_by == RATE_RX ) ? co->color_scheme[SELECTED_H]
                                    : co->color_scheme[HEADER] );
  wprintw ( pad, "    %s   ", "RATE RX" );

  wattrset ( pad,
             ( sort_by == TOT_TX ) ? co->color_scheme[SELECTED_H]
                                   : co->color_scheme[HEADER] );
  wprintw ( pad, "    %s    ", "TOTAL TX" );

  wattrset ( pad,
             ( sort_by == TOT_RX ) ? co->color_scheme[SELECTED_H]
                                   : co->color_scheme[HEADER] );
  wprintw ( pad, "  %s   ", "TOTAL RX" );

  wattrset ( pad, co->color_scheme[HEADER] );
  wprintw ( pad, "%*s", -( COLS_PAD - PROGRAM - 1 ), "PROGRAM" );

  wattrset ( pad, co->color_scheme[RESET] );

  // atualiza cabeçalho
  pnoutrefresh ( pad, 0, scroll_x, 0, 0, 0, COLS - 1 );
}

void
running_input (const struct config_op *co)
{
  int ch;

  while ( ( ch = wgetch ( pad ) ) != ERR )
    {
      switch ( ch )
        {
          // scroll horizontal
          case KEY_RIGHT:
            if ( scroll_x + 5 < COLS_PAD - COLS )
              {
                scroll_x = ( scroll_x + 5 <= COLS_PAD - COLS )
                                   ? scroll_x + 5
                                   : COLS_PAD - COLS;

                prefresh ( pad, 0, scroll_x, 0, 0, LINES - 1, COLS - 1 );
              }
            else
              beep ();

            break;
          case KEY_LEFT:
            if ( scroll_x > 0 )
              {
                scroll_x = ( scroll_x - 5 >= 0 ) ? scroll_x - 5 : 0;

                prefresh ( pad, 0, scroll_x, 0, 0, LINES - 1, COLS - 1 );
              }
            else
              beep ();

            break;
          case KEY_DOWN:
            if ( selected + 1 <= tot_rows )
              {
                selected++;
                if ( selected >= LINES )
                  scroll_y++;

                // restaura linha atual
                mvwaddchnstr ( pad, selected - 1, 0, line_original, COLS_PAD );

                // salva linha que sera marcada/selecionada (antes de estar
                // pintada)
                mvwinchnstr ( pad, selected, 0, line_original, COLS_PAD - 1 );

                // pinta a linha selecionada
                paint_selected (co);

                // atualiza tela
                // getyx(pad, y, x);
                pnoutrefresh (
                        pad, scroll_y, scroll_x, 1, 0, LINES - 1, COLS - 1 );
                doupdate ();
              }
            else
              beep ();

            break;
          case KEY_UP:
            if ( selected - 1 >= 1 )
              {
                selected--;
                if ( scroll_y > 1 && selected <= LINES )
                  scroll_y--;

                // restaura linha atual
                mvwaddchnstr ( pad, selected + 1, 0, line_original, COLS_PAD );

                // salva linha que sera marcada/selecionada (antes de estar
                // pintada)
                mvwinchnstr ( pad, selected, 0, line_original, COLS_PAD - 1 );

                paint_selected (co);

                // atualiza tela
                pnoutrefresh (
                        pad, scroll_y, scroll_x, 1, 0, LINES - 1, COLS - 1 );
                doupdate ();
              }
            else
              beep ();

            break;
          case 's':
          case 'S':
            sort_by = ( sort_by + 1 < COLS_TO_SORT ) ? sort_by + 1 : 0;
            show_header (co);
            doupdate ();
            break;
          case 'q':
          case 'Q':
            exit ( EXIT_SUCCESS );
        }
    }
}

static void
paint_selected (const struct config_op *co)
{
  int i = 0;
  bool skip = false;

  while ( i < COLS_PAD )
    {
      // line_color[i] = line_original[i];
      //  // retira atributos
      // line_color[i] &= A_CHARTEXT | A_ALTCHARSET;
      // line_color[i] |= COLOR_PAIR(4);  // adiciona atributos
      // novo
      if ( skip )
        goto SKIP;

      if ( line_original[i] )
        waddch ( pad,
                 ( line_original[i] & ( A_CHARTEXT | A_ALTCHARSET ) ) |
                         co->color_scheme[SELECTED_L] );
      else
        skip = true;

    SKIP:
      if ( skip )
        waddch ( pad, ' ' | co->color_scheme[SELECTED_L] );

      i++;
    }
}
