/**
 * author:  Viktar Vasiuk

   ----------------------------------------------------------------------
    Copyright (C) Viktar Vasiuk, 2023
    
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    any later version.
     
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
   ----------------------------------------------------------------------

@see https://github.com/vivask/wifi-manager
*/
#include <stdlib.h>
#include "list.h"

void push_cb(CallBackList **head, Func func) {
    CallBackList *tmp = (CallBackList*) malloc(sizeof(CallBackList));
    tmp->func = func;
    tmp->next = (*head);
    (*head) = tmp;
}

void run_cb(const CallBackList *head, void* param) {
    while(head) {
        head->func(param);
        head = head->next;
    }
}