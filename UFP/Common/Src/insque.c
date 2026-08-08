/*---------------------------------------------------------------------------
	Project:	      IP400

	Module:			  Queue management

	File Name:	      insque.c

	Author:		      MartinA

	Creation Date:	  Jan 8, 2025

	Description:      Port of Linux insque/remque for IP400 use

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.


	Revision History:
---------------------------------------------------------------------------*/

struct qelem {
  struct qelem *q_forw;
  struct qelem *q_back;
};


void insque (struct qelem *elem, struct qelem *pred)
{
  elem -> q_forw = pred -> q_forw;
  pred -> q_forw -> q_back = elem;
  elem -> q_back = pred;
  pred -> q_forw = elem;
}


void remque (struct qelem *elem)
{
  elem -> q_forw -> q_back = elem -> q_back;
  elem -> q_back -> q_forw = elem -> q_forw;
}
